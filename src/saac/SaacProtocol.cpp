#include "saac/Api.h"

#include <cstring>

namespace SA::Saac
{

std::vector<std::uint8_t> encodeRequest(PacketType type, const RequestEnvelope &env,
                                        const std::vector<std::uint8_t> &payload)
{
	const std::uint32_t header_len = 4 + 2 + 4 + 4 + 8 + 8;
	const std::uint32_t total_len = header_len + static_cast<std::uint32_t>(payload.size());
	std::vector<std::uint8_t> buf(total_len);

	std::uint8_t *p = buf.data();
	std::memcpy(p, &total_len, 4);
	p += 4;
	const std::uint16_t t = static_cast<std::uint16_t>(type);
	std::memcpy(p, &t, 2);
	p += 2;
	std::memcpy(p, &env.instance_id, 4);
	p += 4;
	std::memcpy(p, &env.generation, 4);
	p += 4;
	std::memcpy(p, &env.request_id, 8);
	p += 8;
	std::memcpy(p, &env.deadline_ms, 8);
	p += 8;

	if (!payload.empty())
	{
		std::memcpy(p, payload.data(), payload.size());
	}
	return buf;
}

bool decodeResponse(const std::uint8_t *data, std::size_t size,
                    PacketType &out_type, ResponseEnvelope &out_env,
                    std::vector<std::uint8_t> &out_payload)
{
	const std::size_t min_len = 4 + 2 + 4 + 4 + 8 + 4 + 4;
	if (size < min_len)
		return false;

	std::uint32_t total_len = 0;
	std::memcpy(&total_len, data, 4);
	if (size < total_len || total_len < min_len)
		return false;

	const std::uint8_t *p = data + 4;
	std::uint16_t t = 0;
	std::memcpy(&t, p, 2);
	p += 2;
	out_type = static_cast<PacketType>(t);

	std::memcpy(&out_env.instance_id, p, 4);
	p += 4;
	std::memcpy(&out_env.generation, p, 4);
	p += 4;
	std::memcpy(&out_env.request_id, p, 8);
	p += 8;

	std::uint32_t status_val = 0;
	std::memcpy(&status_val, p, 4);
	p += 4;
	out_env.status = static_cast<SaacAccountStatus>(status_val);

	std::memcpy(&out_env.error_code, p, 4);
	p += 4;

	const std::size_t payload_len = total_len - min_len;
	if (payload_len > 0)
	{
		out_payload.assign(p, p + payload_len);
	}
	else
	{
		out_payload.clear();
	}
	return true;
}

} // namespace SA::Saac
