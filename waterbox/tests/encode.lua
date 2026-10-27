-- Records the run the project holds with Chimera's Encode Video, so the gate
-- can read the file back (run-frontend-gate.sh). The environment says where:
--   ARES_ENCODE_OUT=<the video>   ARES_ENCODE_FRAMES=<how many frames>
local out = os.getenv("ARES_ENCODE_OUT")
local frames = tonumber(os.getenv("ARES_ENCODE_FRAMES"))
if out == nil or frames == nil then error("ARES_ENCODE_OUT and ARES_ENCODE_FRAMES are not set") end
local err = client.encodevideo(out, 0, frames - 1, "-c:v ffv1 -c:a pcm_s16le -f avi")
if err ~= nil then
	print("encode refused: " .. err)
	client.exit()
end
while client.encodingvideo() do emu.frameadvance() end
client.exit()
