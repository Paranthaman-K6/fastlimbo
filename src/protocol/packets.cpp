#include "protocol/packets.h"
#include "protocol/buffer.h"

namespace limbo::packets {

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}

std::vector<uint8_t> statusResponseBody(const std::string& versionName, int protocol,
                                        int maxPlayers, int online, const std::string& motd) {
  std::string json = "{\"version\":{\"name\":\"" + jsonEscape(versionName) +
                     "\",\"protocol\":" + std::to_string(protocol) +
                     "},\"players\":{\"max\":" + std::to_string(maxPlayers) +
                     ",\"online\":" + std::to_string(online) +
                     "},\"description\":{\"text\":\"" + jsonEscape(motd) + "\"}}";
  proto::Writer w;
  w.str(json);
  return w.b;
}

// NBT StringTag anonymous: [type=8][u16 len][bytes]. For modern text component we send
// TAG_String whose value is JSON chat, e.g. {"text":"msg"}. Vanilla accepts StringTag.
static void writeAnonymousNbtString(proto::Writer& w, const std::string& json) {
  w.u8(8);  // TAG_String
  w.u16(static_cast<uint16_t>(json.size()));
  w.bytes(reinterpret_cast<const uint8_t*>(json.data()), json.size());
}

std::vector<uint8_t> loginDisconnectBody(int pvn, const std::string& message) {
  std::string json = "{\"text\":\"" + jsonEscape(message) + "\"}";
  proto::Writer w;
  if (pvn >= 765) writeAnonymousNbtString(w, json);  // 1.20.3+ NBT
  else w.str(json);
  return w.b;
}

std::vector<uint8_t> loginSuccessBody(int pvn, const std::array<uint8_t,16>& uuid,
                                      const std::string& username) {
  proto::Writer w;
  w.uuid(uuid);
  w.str(username);
  if (pvn >= 759) w.varInt(0);  // 1.19+ properties count
  return w.b;
}

std::vector<uint8_t> loginPluginRequestBody(int32_t msgId, const std::string& channel,
                                            const std::vector<uint8_t>& data) {
  proto::Writer w;
  w.varInt(msgId);
  w.str(channel);
  w.bytes(data);
  return w.b;
}

std::vector<uint8_t> playDisconnectBody(int pvn, const std::string& message) {
  return loginDisconnectBody(pvn, message);
}

std::vector<uint8_t> configDisconnectBody(int pvn, const std::string& message) {
  return loginDisconnectBody(pvn, message);
}

}  // namespace limbo::packets
