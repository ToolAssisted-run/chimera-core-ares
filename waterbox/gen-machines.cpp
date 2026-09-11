/* gen-machines - asks ares what its machines are made of.
 *
 * The wire format of a controller is what a movie is written in: index 7 has to
 * mean the same button next year as it did when the movie was recorded. So it
 * is not hand-maintained. This tool builds each machine, walks ares' own node
 * graph for the inputs of every device this core offers, and prints the lot as
 * JSON with the exact path of each input; gen-config.py turns that into
 * waterbox.config's machines[] and the table the guest binds against.
 *
 * Both outputs are committed, and the gate regenerates them and diffs, so a
 * newer ares that renames or reorders a button cannot quietly renumber
 * somebody's movie - it fails the gate instead.
 *
 *   gen-machines [firmware-dir] > machines.json
 *
 * A machine that needs a console BIOS can only be described when one is there
 * to build it with, and nothing of the kind is in this repository. Point the
 * tool at a directory holding a file named after each firmware id
 * (tests/firmware/gbaBios and friends) and those machines are described too;
 * without it they are reported as unbuilt, and the gate checks only what it
 * could build.
 */
#include "machines.h"

#include <mia/mia.hpp>

#include <stdio.h>
#include <map>
#include <string>
#include <vector>

using namespace ares;

namespace
{
	std::shared_ptr<mia::Pak> g_systemPak;

	struct GenPlatform : ares::Platform
	{
		auto pak(Node::Object) -> std::shared_ptr<vfs::directory> override
		{
			return g_systemPak ? g_systemPak->pak : nullptr;
		}
	};

	std::string escape(const std::string &in)
	{
		std::string out;
		for (char c : in)
		{
			if (c == '"' || c == '\\') out += '\\';
			out += c;
		}
		return out;
	}

	struct Input
	{
		std::string path;  /* what root->find() resolves, e.g. "Controller Port 1/Gamepad/A" */
		std::string name;  /* the leaf, e.g. "A" */
		bool isAxis;
	};

	/* Every input in the tree, with the path that reaches it. Order is ares'
	 * own, depth first, which is the order the machine was built in. */
	void walk(Node::Object node, const std::string &prefix, std::vector<Input> &out)
	{
		for (auto &child : *node)
		{
			std::string name = (const char *)child->name();
			std::string path = prefix.empty() ? name : prefix + "/" + name;
			if (child->cast<Node::Input::Axis>()) out.push_back({path, name, true});
			else if (child->cast<Node::Input::Button>()) out.push_back({path, name, false});
			walk(child, path, out);
		}
	}

	/* A machine's own settings, as ares declares them. Every setting type shares
	 * one string-shaped interface on the base - readValue, readAllowedValues,
	 * writeValue - so this needs to know nothing about which kind each is beyond
	 * saying so, and the guest applies them back the same way. */
	struct Setting
	{
		std::string path;
		std::string name;
		std::string type;     /* bool, int or enum */
		std::string value;    /* what ares powers on with */
		std::vector<std::string> options;
	};

	void walkSettings(Node::Object node, const std::string &prefix, std::vector<Setting> &out)
	{
		for (auto &child : *node)
		{
			std::string name = (const char *)child->name();
			std::string path = prefix.empty() ? name : prefix + "/" + name;
			if (auto setting = child->cast<Node::Setting::Setting>())
			{
				Setting s;
				s.path = path;
				s.name = name;
				s.value = (const char *)setting->readValue();
				for (auto &v : setting->readAllowedValues()) s.options.push_back((const char *)v);
				if (child->cast<Node::Setting::Boolean>()) s.type = "bool";
				else if (!s.options.empty()) s.type = "enum";
				else if (child->cast<Node::Setting::Natural>() || child->cast<Node::Setting::Integer>()) s.type = "int";
				else s.type = "string";
				out.push_back(s);
			}
			walkSettings(child, path, out);
		}
	}

	void printSettings(const std::vector<Setting> &settings)
	{
		bool first = true;
		printf("[");
		for (auto &s : settings)
		{
			if (!first) printf(", ");
			first = false;
			printf("{\"name\": \"%s\", \"path\": \"%s\", \"type\": \"%s\", \"value\": \"%s\", \"options\": [",
				escape(s.name).c_str(), escape(s.path).c_str(), s.type.c_str(), escape(s.value).c_str());
			bool firstOpt = true;
			for (auto &o : s.options)
			{
				if (!firstOpt) printf(", ");
				firstOpt = false;
				printf("\"%s\"", escape(o).c_str());
			}
			printf("]}");
		}
		printf("]");
	}

	bool known(const std::vector<Input> &haystack, const std::string &path)
	{
		for (auto &i : haystack) if (i.path == path) return true;
		return false;
	}

	void printInputs(const std::vector<Input> &inputs, const std::vector<Input> *except)
	{
		bool first = true;
		printf("[");
		for (auto &i : inputs)
		{
			if (except && known(*except, i.path)) continue;
			if (!first) printf(", ");
			first = false;
			printf("{\"name\": \"%s\", \"path\": \"%s\", \"axis\": %s}",
				escape(i.name).c_str(), escape(i.path).c_str(), i.isAxis ? "true" : "false");
		}
		printf("]");
	}

	/* The devices this core offers for a port: what the machine's spec asks for,
	 * filtered to what ares actually supports there. A spec that names nothing
	 * gets the port's own first choice, which is the ordinary controller. */
	std::vector<std::string> offered(const machines::Spec &spec, Node::Port port)
	{
		auto supported = port->supported();
		std::vector<std::string> result;
		if (spec.devices == nullptr || !*spec.devices)
		{
			if (!supported.empty()) result.push_back((const char *)supported.front());
			return result;
		}
		std::string wanted = spec.devices;
		size_t at = 0;
		while (at < wanted.size())
		{
			size_t end = wanted.find(' ', at);
			if (end == std::string::npos) end = wanted.size();
			std::string want = wanted.substr(at, end - at);
			at = end + 1;
			if (want.empty()) continue;
			for (auto &have : supported)
			{
				if (want == (const char *)have) result.push_back(want);
			}
		}
		return result;
	}
}

int main(int argc, char **argv)
{
	const char *firmwareDir = argc > 1 ? argv[1] : nullptr;
	static GenPlatform platform;
	ares::platform = &platform;

	printf("{\n");
	printf("  \"_comment\": \"Generated by waterbox/gen-machines from ares itself. Do not edit; run waterbox/gen-config.py.\",\n");
	printf("  \"machines\": [\n");

	bool firstMachine = true;
	for (auto &spec : machines::all())
	{
		g_systemPak = mia::System::create(spec.miaSystem);
		string firmware;
		std::vector<string> firmwares;
		bool firmwareMissing = false;
		if (spec.firmware != nullptr)
		{
			if (firmwareDir == nullptr) firmwareMissing = true;
			else
			{
				firmware = {firmwareDir, "/", spec.firmware};
				if (!file::exists(firmware)) firmwareMissing = true;
				firmwares.push_back(firmware);
				for (const char *const *extra = spec.extraFirmware; extra && *extra; extra++)
				{
					string one = {firmwareDir, "/", *extra};
					if (!file::exists(one)) firmwareMissing = true;
					firmwares.push_back(one);
				}
			}
		}
		bool systemOk = !firmwareMissing && g_systemPak
			&& (spec.extraFirmware != nullptr ? g_systemPak->loadMultiple(firmwares)
			                                  : g_systemPak->load(firmware) == successful);

		/* What this machine has to be told before it is built. A machine that
		 * chooses between two implementations of a chip through an option has a
		 * null pointer until it is told, and System::load walks straight through
		 * it - so skipping this crashes rather than failing. See machines.h. */
		if (spec.option != nullptr && spec.options != nullptr)
		{
			for (const machines::Option *o = spec.options; o->name != nullptr; o++)
			{
				spec.option(o->name, o->value);
			}
		}

		Node::System root;
		bool ok = systemOk && spec.load(root, spec.configNtsc ? spec.configNtsc : spec.configPal);

		if (!firstMachine) printf(",\n");
		firstMachine = false;
		printf("    {\n");
		printf("      \"id\": \"%s\",\n      \"label\": \"%s\",\n", spec.id, escape(spec.label).c_str());
		printf("      \"extensions\": \"%s\",\n", spec.extensions);
		printf("      \"maxWidth\": %d,\n      \"maxHeight\": %d,\n", spec.maxWidth, spec.maxHeight);
		printf("      \"virtualWidth\": %d,\n      \"virtualHeight\": %d,\n", spec.virtualWidth, spec.virtualHeight);
		printf("      \"regions\": [\"ntsc\"%s],\n", spec.configPal ? ", \"pal\"" : "");
		printf("      \"bootsWithoutMedium\": %s,\n", spec.bootsWithoutMedium ? "true" : "false");
		if (spec.subSlots != nullptr)
		{
			/* What this machine's CARTRIDGE can take a second file of, so the
			 * package can offer a second slot and offer it only here. */
			printf("      \"subExtensions\": \"");
			bool firstSub = true;
			for (const machines::SubSlot *sub = spec.subSlots; sub->extensions != nullptr; sub++)
			{
				printf("%s%s", firstSub ? "" : " ", sub->extensions);
				firstSub = false;
			}
			printf("\",\n");
		}
		if (spec.firmware != nullptr)
		{
			/* Only the ids: gen-config.py finds each file under tests/firmware
			 * and hashes it, so the package pins the BIOSes that were verified
			 * to work and this file stays the same whatever path it was run
			 * with. A list because a machine may need several, in the order the
			 * system pak reads them. */
			printf("      \"firmware\": [{\"id\": \"%s\"}", spec.firmware);
			for (const char *const *extra = spec.extraFirmware; extra && *extra; extra++)
			{
				printf(", {\"id\": \"%s\"}", *extra);
			}
			printf("],\n");
		}
		printf("      \"loads\": %s,\n", ok ? "true" : "false");
		if (!ok)
		{
			printf("      \"why\": \"%s\"\n    }",
				firmwareMissing ? "its console BIOS was not provided to this run"
				                : (systemOk ? "ares would not build it" : "its system pak would not load"));
			continue;
		}

		/* With nothing plugged in, everything the tree offers belongs to the
		 * console itself: a Reset switch, a handheld's own buttons. */
		std::vector<Input> console;
		walk(root, "", console);

		printf("      \"console\": ");
		printInputs(console, nullptr);

		/* The machine's own settings, before anything is plugged in: a Game Boy's
		 * Fast Boot, a WonderSwan's Headphones, an Aleck 64's coin slot. */
		std::vector<Setting> settings;
		walkSettings(root, "", settings);
		printf(",\n      \"settings\": ");
		printSettings(settings);

		printf(",\n      \"ports\": [\n");

		bool firstPort = true;
		for (auto &port : root->find<Node::Port>())
		{
			if (port->supported().empty()) continue;  /* a media slot, not a controller port */
			auto devices = offered(spec, port);
			if (devices.empty()) continue;

			if (!firstPort) printf(",\n");
			firstPort = false;
			printf("        {\n          \"name\": \"%s\",\n          \"devices\": [\n",
				escape((const char *)port->name()).c_str());

			bool firstDevice = true;
			for (auto &deviceName : devices)
			{
				auto peripheral = port->allocate(deviceName.c_str());
				if (!peripheral) continue;
				port->connect();

				std::vector<Input> withDevice;
				walk(root, "", withDevice);

				if (!firstDevice) printf(",\n");
				firstDevice = false;
				printf("            {\n              \"name\": \"%s\",\n              \"inputs\": ",
					escape(deviceName).c_str());
				printInputs(withDevice, &console);
				printf("\n            }");

				port->disconnect();
			}
			printf("\n          ]\n        }");
		}
		printf("\n      ]\n    }");
	}

	printf("\n  ]\n}\n");
	return 0;
}
