#include "SAMPRakNet.hpp"
#include <climits>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <mutex>
#include <unordered_set>

#include <RakPeer.h>
#include <SocketLayer.h>
#include <PacketEnumerations.h>

uint8_t SAMPRakNet::decryptBuffer_[MAXIMUM_MTU_SIZE];
uint8_t SAMPRakNet::encryptBuffer_[MAXIMUM_MTU_SIZE];
uint32_t SAMPRakNet::token_;
uint16_t SAMPRakNet::portNumber = 7777;
OmpQueryFn SAMPRakNet::query_ = nullptr;
unsigned int SAMPRakNet::timeout_ = 10000;
unsigned int SAMPRakNet::minConnectionTime_ = 0;
unsigned int SAMPRakNet::messagesLimit_ = 500;
unsigned int SAMPRakNet::messageHoleLimit_ = 3000;
unsigned int SAMPRakNet::acksLimit_ = 3000;
unsigned int SAMPRakNet::networkLimitsBanTime_ = 60000;
float SAMPRakNet::minimumSendBitsPerSecond_ = 96000.0f;
bool SAMPRakNet::logCookies_ = false;
ICore* SAMPRakNet::core_ = nullptr;
FlatHashSet<uint32_t> SAMPRakNet::incomingConnections_;
RakNet::RakNetTime SAMPRakNet::gracePeriod_ = 0;
FlatHashMap<PlayerAddressHash, SAMPRakNet::OmpPlayerEncryptionData> SAMPRakNet::ompPlayers_;

uint16_t
SAMPRakNet::
    GetPort()
{
    return portNumber;
}

void SAMPRakNet::
    SetPort(uint16_t value)
{
    portNumber = value;
}

uint8_t*
SAMPRakNet::
    Decrypt(uint8_t const* src, int len)
{
    static const uint8_t
        key[256]
        = {
              0xB4,
              0x62,
              0x07,
              0xE5,
              0x9D,
              0xAF,
              0x63,
              0xDD,
              0xE3,
              0xD0,
              0xCC,
              0xFE,
              0xDC,
              0xDB,
              0x6B,
              0x2E,
              0x6A,
              0x40,
              0xAB,
              0x47,
              0xC9,
              0xD1,
              0x53,
              0xD5,
              0x20,
              0x91,
              0xA5,
              0x0E,
              0x4A,
              0xDF,
              0x18,
              0x89,
              0xFD,
              0x6F,
              0x25,
              0x12,
              0xB7,
              0x13,
              0x77,
              0x00,
              0x65,
              0x36,
              0x6D,
              0x49,
              0xEC,
              0x57,
              0x2A,
              0xA9,
              0x11,
              0x5F,
              0xFA,
              0x78,
              0x95,
              0xA4,
              0xBD,
              0x1E,
              0xD9,
              0x79,
              0x44,
              0xCD,
              0xDE,
              0x81,
              0xEB,
              0x09,
              0x3E,
              0xF6,
              0xEE,
              0xDA,
              0x7F,
              0xA3,
              0x1A,
              0xA7,
              0x2D,
              0xA6,
              0xAD,
              0xC1,
              0x46,
              0x93,
              0xD2,
              0x1B,
              0x9C,
              0xAA,
              0xD7,
              0x4E,
              0x4B,
              0x4D,
              0x4C,
              0xF3,
              0xB8,
              0x34,
              0xC0,
              0xCA,
              0x88,
              0xF4,
              0x94,
              0xCB,
              0x04,
              0x39,
              0x30,
              0x82,
              0xD6,
              0x73,
              0xB0,
              0xBF,
              0x22,
              0x01,
              0x41,
              0x6E,
              0x48,
              0x2C,
              0xA8,
              0x75,
              0xB1,
              0x0A,
              0xAE,
              0x9F,
              0x27,
              0x80,
              0x10,
              0xCE,
              0xF0,
              0x29,
              0x28,
              0x85,
              0x0D,
              0x05,
              0xF7,
              0x35,
              0xBB,
              0xBC,
              0x15,
              0x06,
              0xF5,
              0x60,
              0x71,
              0x03,
              0x1F,
              0xEA,
              0x5A,
              0x33,
              0x92,
              0x8D,
              0xE7,
              0x90,
              0x5B,
              0xE9,
              0xCF,
              0x9E,
              0xD3,
              0x5D,
              0xED,
              0x31,
              0x1C,
              0x0B,
              0x52,
              0x16,
              0x51,
              0x0F,
              0x86,
              0xC5,
              0x68,
              0x9B,
              0x21,
              0x0C,
              0x8B,
              0x42,
              0x87,
              0xFF,
              0x4F,
              0xBE,
              0xC8,
              0xE8,
              0xC7,
              0xD4,
              0x7A,
              0xE0,
              0x55,
              0x2F,
              0x8A,
              0x8E,
              0xBA,
              0x98,
              0x37,
              0xE4,
              0xB2,
              0x38,
              0xA1,
              0xB6,
              0x32,
              0x83,
              0x3A,
              0x7B,
              0x84,
              0x3C,
              0x61,
              0xFB,
              0x8C,
              0x14,
              0x3D,
              0x43,
              0x3B,
              0x1D,
              0xC3,
              0xA2,
              0x96,
              0xB3,
              0xF8,
              0xC4,
              0xF2,
              0x26,
              0x2B,
              0xD8,
              0x7C,
              0xFC,
              0x23,
              0x24,
              0x66,
              0xEF,
              0x69,
              0x64,
              0x50,
              0x54,
              0x59,
              0xF1,
              0xA0,
              0x74,
              0xAC,
              0xC6,
              0x7D,
              0xB5,
              0xE6,
              0xE2,
              0xC2,
              0x7E,
              0x67,
              0x17,
              0x5E,
              0xE1,
              0xB9,
              0x3F,
              0x6C,
              0x70,
              0x08,
              0x99,
              0x45,
              0x56,
              0x76,
              0xF9,
              0x9A,
              0x97,
              0x19,
              0x72,
              0x5C,
              0x02,
              0x8F,
              0x58,
          };
    uint8_t
        cur,
        checksum = 0,
        port = GetPort() ^ 0xCC;
    for (int i = 1; i != len; ++i) {
        // Alternate the mask every byte.
        cur = (uint8_t)src[i];
        if (!(i & 1))
            cur ^= port;
        cur = key[cur];
        checksum ^= cur & 0xAA;
		decryptBuffer_[i - 1] = cur;
    }
    if (src[0] != checksum) {
        return nullptr;
    }
	return decryptBuffer_;
}

// MP 5.02: the client side of Decrypt (a phone sending to a PC server): checksum of the plain bytes first,
// then every byte through the inverse table, every second byte xor (port ^ 0xCC).
uint8_t*
SAMPRakNet::
    EncryptClient(uint8_t const* src, int len, uint16_t serverPort, int* outLen)
{
    static thread_local uint8_t out[MAXIMUM_MTU_SIZE + 1];
    static uint8_t inv[256];
    static bool invReady = false;
    if (!invReady) {
        uint8_t probe[2] = { 0, 0 };
        // Decrypt's table is private to it: rebuild the inverse by asking Decrypt for every byte
        // (odd position, no port xor): Decrypt({chk, x}) gives key[x]
        const uint16_t savedPort = portNumber;
        for (int x = 0; x < 256; ++x) {
            probe[1] = (uint8_t)x;
            // checksum must match: try both possible values of key[x] & 0xAA
            for (int c = 0; c < 256; ++c) {
                probe[0] = (uint8_t)c;
                if (uint8_t* d = Decrypt(probe, 2)) { inv[d[0]] = (uint8_t)x; break; }
            }
        }
        portNumber = savedPort;
        invReady = true;
    }
    if (len < 0 || len > MAXIMUM_MTU_SIZE) { *outLen = 0; return out; }
    const uint8_t port = (uint8_t)(serverPort ^ 0xCC);
    uint8_t checksum = 0;
    for (int j = 0; j < len; ++j) {
        checksum ^= src[j] & 0xAA;
        uint8_t c = inv[src[j]];
        if (j & 1) c ^= port;   // position i = j + 1 even -> Decrypt xors it
        out[j + 1] = c;
    }
    out[0] = checksum;
    *outLen = len + 1;
    return out;
}

uint8_t*
SAMPRakNet::
	Encrypt(const OmpPlayerEncryptionData* encryptionData, uint8_t const* src, int len)
{
	const auto key = encryptionData->key;
    uint8_t checksum = 0;

    uint8_t keyBytes[ENCRYPTION_KEY_SIZE];
	keyBytes[0] = static_cast<uint8_t>(key & 0xFF);
	keyBytes[1] = static_cast<uint8_t>((key >> 8) & 0xFF);
	keyBytes[2] = static_cast<uint8_t>((key >> 16) & 0xFF);
	keyBytes[3] = static_cast<uint8_t>((key >> 24) & 0xFF);

	for (size_t i = 0; i != len; ++i)
	{
		auto cur = src[i] ^ keyBytes[i % ENCRYPTION_KEY_SIZE];
		checksum ^= src[i] & 0xAA;
		encryptBuffer_[i + 1] = cur;
	}

	encryptBuffer_[0] = checksum;
	return encryptBuffer_;
}

void SAMPRakNet::
    HandleQuery(SOCKET instance, int outsize, const sockaddr_in& client, char const* buf, int insize)
{
    if (query_ == nullptr) {
        return;
    }
    static thread_local char out[2048];
    const int n = query_(buf, insize, out, (int)sizeof(out));
    if (n > 0) {
        sendto(instance, out, n, 0, reinterpret_cast<const sockaddr*>(&client), outsize);
    }
}

struct AuthEntry {
    StringView send;
    StringView recv;
};
#include "samp_auth_table.inc"
static const int kAuthCount = (int)(sizeof(AuthTable) / sizeof(AuthTable[0]));

Pair<uint8_t, StringView> SAMPRakNet::GenerateAuth()
{
    const uint8_t rnd = (uint8_t)(rand() % kAuthOmpCount);
    return Pair<uint8_t, StringView> { rnd, AuthTable[rnd].send };
}

bool SAMPRakNet::CheckAuth(uint8_t index, StringView auth)
{
    if (index >= kAuthOmpCount) return false;
    return AuthTable[index].recv == auth;
}

// MP 5.02 client side: the response a SA-MP 0.3.7 client gives to a server challenge (nullptr = unknown)
const char* SAMPRakNet::AuthResponse(const char* challenge)
{
    if (!challenge) return nullptr;
    const StringView c(challenge);
    for (int i = 0; i < kAuthCount; ++i) {
        if (AuthTable[i].send == c) return AuthTable[i].recv.data();
    }
    return nullptr;
}

// MP 5.02: sockets owned by a RakClient talking to a SA-MP/open.mp server
static std::mutex g_clientSockMx;
static std::unordered_set<SOCKET> g_clientSocks;

void SAMPRakNet::SetClientSocket(SOCKET s, bool client)
{
    std::lock_guard<std::mutex> lk(g_clientSockMx);
    if (client) g_clientSocks.insert(s); else g_clientSocks.erase(s);
}

bool SAMPRakNet::IsClientSocket(SOCKET s)
{
    std::lock_guard<std::mutex> lk(g_clientSockMx);
    return !g_clientSocks.empty() && g_clientSocks.count(s) != 0;
}

uint16_t cookies[2][256];

void SAMPRakNet::SeedCookie()
{
    for (int i = 0; i < 256; ++i) {
        cookies[0][i] = rand() % (USHRT_MAX + 1);
        cookies[1][i] = rand() % (USHRT_MAX + 1);
    }
}

uint16_t SAMPRakNet::GetCookie(unsigned int address)
{
    const uint8_t* addressSplit = (const uint8_t*)&address;
    return (cookies[0][addressSplit[0]] | cookies[1][addressSplit[3]] << 8) ^ ((addressSplit[1] << 8) | addressSplit[2]);
}

void SAMPRakNet::ReplyToOmpClientAccessRequest(SOCKET connectionSocket, const RakNet::PlayerID& playerId, uint32_t encryptionKey)
{
	if (IsOmpEncryptionEnabled())
	{
		int len = 13;
		char c[13];
		c[0] = RakNet::ID_USER_PACKET_ENUM;
		*(uint32_t*)&c[1] = MAGIC_OMP_IDENTIFICATION_NUMBER;
		*(uint32_t*)&c[5] = encryptionKey;
		*(uint32_t*)&c[9] = uint32_t(CURRENT_OMP_CLIENT_MOD_VERSION);

		RakNet::SocketLayer::Instance()->SendTo(connectionSocket, (const char*)&c, len, playerId.binaryAddress, playerId.port);
	}

	ConfigurePlayerUsingOmp(playerId, encryptionKey);
}

bool SAMPRakNet::OnConnectionRequest(
    SOCKET connectionSocket,
    RakNet::PlayerID& playerId,
    const char* data,
    RakNet::RakNetTime& minConnectionTick,
    RakNet::RakNetTime& minConnectionLogTick
)
{
	ResetOmpPlayerConfiguration(playerId);

	if (playerId.binaryAddress == LOCALHOST)
	{
		// Allow unlimited connections from localhost (testing and bots).
	}
	else
	{
		RakNet::RakNetTime tickTime = RakNet::GetTime();
		if (tickTime < gracePeriod_)
		{
			// Allow unlimited connections during the grace period
		}
		else if (SAMPRakNet::IsAlreadyRequestingConnection(playerId.binaryAddress))
		{
			return false;
		}
		else
		{
			RakNet::RakNetTime configuredMinConnTime = SAMPRakNet::GetMinConnectionTime();
			if (minConnectionTick && configuredMinConnTime > 0 && tickTime - minConnectionTick < configuredMinConnTime)
			{
				if (!minConnectionLogTick || tickTime - minConnectionLogTick > configuredMinConnTime)
				{
					GetCore()->logLn(
						LogLevel::Warning,
						"Minimum time between new connections (%u) exceeded for %s. Ignoring the request.",
						configuredMinConnTime,
						playerId.ToString());
					minConnectionLogTick = tickTime;
				}
				return false;
			}

			minConnectionTick = tickTime;
		}
	}

    uint16_t xordCookie = *(uint16_t*)(data + 1);
	if ((xordCookie ^ SAMP_PETARDED) != (uint16_t)(SAMPRakNet::GetCookie(playerId.binaryAddress)))
	{
		if ((xordCookie ^ OMP_PETARDED) != (uint16_t)(SAMPRakNet::GetCookie(playerId.binaryAddress))) // it's omp
		{
#ifdef _DO_PRINTF
			if (SAMPRakNet::ShouldLogCookies())
			{
				GetCore()->printLn("%s requests connection cookie", playerId.ToString());
			}
#endif
			char c[3];
			c[0] = RakNet::ID_OPEN_CONNECTION_COOKIE;
			*(uint16_t*)&c[1] = (uint16_t)(SAMPRakNet::GetCookie(playerId.binaryAddress));
			RakNet::SocketLayer::Instance()->SendTo(connectionSocket, (const char*)&c, 3, playerId.binaryAddress, playerId.port);
			ResetOmpPlayerConfiguration(playerId);
			return false;
		}
		else
		{
			std::random_device rd; // Non-deterministic seed source
			std::mt19937 gen(rd()); // Mersenne Twister engine
			std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);

			uint32_t randomInt = dist(gen);
			ReplyToOmpClientAccessRequest(connectionSocket, playerId, randomInt);
		}
	}

    return true;
}
