#include "Checksum.hpp"

#include "Utils.hpp"
#include "Modules/Engine.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <map>
#include <filesystem>
#include <mutex>

#define WRITE_LE32(x)          \
	(uint8_t)(x & 0xFF),          \
		(uint8_t)((x >> 8) & 0xFF),  \
		(uint8_t)((x >> 16) & 0xFF), \
		(uint8_t)((x >> 24) & 0xFF)

#define READ_LE32(arr, i) \
	(((uint32_t)arr[i + 0] << 0) | ((uint32_t)arr[i + 1] << 8) | ((uint32_t)arr[i + 2] << 16) | ((uint32_t)arr[i + 3] << 24))

// clang-format off
static const uint32_t crcTable[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
	0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
	0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
	0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
	0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
	0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
	0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
	0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
	0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
	0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
	0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
	0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
	0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
	0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
	0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
	0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
	0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
	0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
	0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
	0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
	0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
	0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
	0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
	0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
	0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
	0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
	0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
	0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
	0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
	0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
	0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
	0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
	0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};
// clang-format on

static uint32_t crc32(const char *buf, size_t len) {
	uint32_t sum = 0xFFFFFFFF;

	// Unrolled loop

	size_t i;

	for (i = 0; i + 10 < len; i += 10) {
		uint8_t lookupIdx0 = (sum ^ buf[i+0]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx0];

		uint8_t lookupIdx1 = (sum ^ buf[i+1]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx1];

		uint8_t lookupIdx2 = (sum ^ buf[i+2]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx2];

		uint8_t lookupIdx3 = (sum ^ buf[i+3]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx3];

		uint8_t lookupIdx4 = (sum ^ buf[i+4]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx4];

		uint8_t lookupIdx5 = (sum ^ buf[i+5]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx5];

		uint8_t lookupIdx6 = (sum ^ buf[i+6]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx6];

		uint8_t lookupIdx7 = (sum ^ buf[i+7]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx7];

		uint8_t lookupIdx8 = (sum ^ buf[i+8]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx8];

		uint8_t lookupIdx9 = (sum ^ buf[i+9]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx9];
	}

	for (; i < len; ++i) {
		uint8_t lookupIdx = (sum ^ buf[i]) & 0xFF;
		sum = (sum >> 8) ^ crcTable[lookupIdx];
	}

	return ~sum;
}

static bool fileChecksum(FILE *fp, size_t ignoreEnd, uint32_t *crcOut) {
	if (fseek(fp, -(long)ignoreEnd, SEEK_END)) return false;

	size_t size = ftell(fp);
	if (size == -1) return false;

	if (fseek(fp, 0, SEEK_SET)) return false;

	// what the fuck c++ why do you not have vlas
	char *buf = (char *)malloc(size);

	fread(buf, 1, size, fp);
	if (ferror(fp)) {
		free(buf);
		return false;
	}

	*crcOut = crc32(buf, size);

	free(buf);
	return true;
}

static uint32_t sarChecksum;

bool AddDemoChecksum(const char *filename) {
	FILE *fp = fopen(filename, "ab+");  // Open for binary appending and reading
	if (!fp) return false;

	uint32_t checksum;
	if (!fileChecksum(fp, 0, &checksum)) {
		fclose(fp);
		return false;
	}

	uint8_t checkBuf[] = {
		0x08,                    // Type: CustomData
		WRITE_LE32(0xFFFFFFFF),  // Tick
		0x00,                    // Slot (TODO: what is this?)

		// CustomData packet data:
		WRITE_LE32(0x00),         // ID - see RecordData for an explanation of why we use 0
		WRITE_LE32(0x11),         // Size: 17 bytes
		WRITE_LE32(0xFFFFFFFF),   // Cursor x
		WRITE_LE32(0xFFFFFFFF),   // Cursor y
		0xFF,                     // First byte of data: SAR message ID (0xFF = checksum)
		WRITE_LE32(checksum),     // Demo checksum
		WRITE_LE32(sarChecksum),  // SAR checksum
	};

	if (fwrite(checkBuf, 1, sizeof checkBuf, fp) != sizeof checkBuf) {
		fclose(fp);
		return false;
	}

	fclose(fp);
	return true;
}

std::pair<VerifyResult, uint32_t> VerifyDemoChecksum(const char *filename) {
	FILE *fp = fopen(filename, "rb");
	if (!fp) return std::pair(VERIFY_BAD_DEMO, 0);

	uint32_t realChecksum;
	if (!fileChecksum(fp, 31, &realChecksum)) {
		fclose(fp);
		return std::pair(VERIFY_BAD_DEMO, 0);
	}

	// The start of the checksum should come 31 bytes before the end
	if (fseek(fp, -31, SEEK_END)) {
		fclose(fp);
		return std::pair(VERIFY_BAD_DEMO, 0);
	}

	uint8_t buf[31];
	fread(buf, 1, sizeof buf, fp);
	if (ferror(fp)) {
		fclose(fp);
		return std::pair(VERIFY_BAD_DEMO, 0);
	}

	// We've got all the data we need from the file
	fclose(fp);

	if (buf[0] != 0x08 || READ_LE32(buf, 1) != 0xFFFFFFFF || buf[5] != 0x00) {
		// Couldn't find checksum field!
		return std::pair(VERIFY_NO_CHECKSUM, 0);
	}

	if (READ_LE32(buf, 6) != 0x00 || (READ_LE32(buf, 10) != 0x11 && READ_LE32(buf, 10) != 0x0D)  // workaround for bug in initial 1.12 release
	    || READ_LE32(buf, 14) != 0xFFFFFFFF || READ_LE32(buf, 18) != 0xFFFFFFFF || buf[22] != 0xFF) {
		// SAR message field not a valid checksum!
		return std::pair(VERIFY_NO_CHECKSUM, 0);
	}

	uint32_t storedChecksum = READ_LE32(buf, 23);
	uint32_t storedSarChecksum = READ_LE32(buf, 27);

	VerifyResult res = realChecksum == storedChecksum ? VERIFY_VALID_CHECKSUM : VERIFY_INVALID_CHECKSUM;

	return std::pair(res, storedSarChecksum);
}

#define NUM_FILE_SUM_THREADS 1

static std::thread g_sumthreads[NUM_FILE_SUM_THREADS];
static std::map<std::string, uint32_t> g_filesums[NUM_FILE_SUM_THREADS];

static std::thread g_mapsumthread;
static std::vector<std::string> g_mapfiles;
static std::map<std::string, uint32_t> g_mapsums;
static std::mutex g_mapsums_mutex;

static void calcFileSums(std::map<std::string, uint32_t> *out, std::vector<std::string> paths) {
	for (auto &path : paths) {
		uint32_t sum = 0; // if error, just use 0

		FILE *fp = fopen(path.c_str(), "rb");  // Open for binary reading
		if (fp) {
			fileChecksum(fp, 0, &sum);
			fclose(fp);
		}

		(*out)[path] = sum;
	}
}

static void calcMapSums() {
	for (auto &path : g_mapfiles) {
		uint32_t sum = 0; // if error, just use 0

		FILE *fp = fopen(path.c_str(), "rb");  // Open for binary reading
		if (fp) {
			fileChecksum(fp, 0, &sum);
			fclose(fp);
		}

		std::lock_guard<std::mutex> guard(g_mapsums_mutex);
		g_mapsums[path] = sum;
	}
}

static void initFileSums() {
	std::vector<std::string> paths;
	std::vector<std::string> maps;
	for (auto &ent : std::filesystem::recursive_directory_iterator(".")) {
		if (ent.status().type() == std::filesystem::file_type::regular || ent.status().type() == std::filesystem::file_type::symlink) {
			auto path = ent.path().string();
			std::replace(path.begin(), path.end(), '\\', '/');
			if (Utils::EndsWith(path, ".nut")
				|| (Utils::EndsWith(path, ".vpk") && path.find("portal2_dlc3") != std::string::npos)
				|| path.find("scripts/talker") != std::string::npos)
			{
				paths.push_back(path);
			}

			if (Utils::EndsWith(path, ".bsp") && path.find("/workshop/") == std::string::npos) {
				g_mapfiles.push_back(path);
			}
		}
	}

	size_t idx = 0;
	for (size_t i = 0; i < NUM_FILE_SUM_THREADS; ++i) {
		size_t end = paths.size() * (i+1) / NUM_FILE_SUM_THREADS;
		g_sumthreads[i] = std::thread(calcFileSums, &g_filesums[i], std::vector<std::string>(paths.begin() + idx, paths.begin() + end));
		idx = end;
	}

	g_mapsumthread = std::thread(calcMapSums);
}

static void addFileChecksum(const char *path, uint32_t sum) {
	size_t bufLen = strlen(path) + 6;
	uint8_t *buf = new uint8_t[bufLen];

	buf[0] = 0x0C;
	*(uint32_t *)(buf + 1) = sum;
	strcpy((char *)(buf + 5), path);
	engine->demorecorder->RecordData(buf, bufLen);

	delete[] buf;
}

static void addDemoMapSum(std::string path) {
	std::lock_guard<std::mutex> guard(g_mapsums_mutex);

	// is it calculated yet?
	auto it = g_mapsums.find(path);
	uint32_t sum;

	if (it == g_mapsums.end()) {
		// not calculated - add it now
		sum = 0; // if error, use 0
		FILE *fp = fopen(path.c_str(), "rb");  // Open for binary reading
		if (fp) {
			fileChecksum(fp, 0, &sum);
			fclose(fp);
		}
		g_mapsums[path] = sum;
	} else {
		sum = it->second;
	}

	addFileChecksum(path.c_str(), sum);
}

void AddDemoFileChecksums() {
	// make sure all file sums are fully calculated first
	for (auto &thrd : g_sumthreads) {
		if (thrd.joinable()) thrd.join();
	}

	for (auto &sums : g_filesums) {
		for (auto [path, sum] : sums) {
			addFileChecksum(path.c_str(), sum);
		}
	}

	// find the matching map file(s)

	std::string map = engine->GetCurrentMapName();
	map = "/" + map + ".bsp";

	for (auto &s : g_mapfiles) {
		if (Utils::EndsWith(s, map)) {
			addDemoMapSum(s);
		}
	}
}

void InitSARChecksum() {
	initFileSums();

	std::string path = Utils::GetSARPath();

	FILE *fp = fopen(path.c_str(), "rb");  // Open for binary reading
	if (!fp) return;

	fileChecksum(fp, 0, &sarChecksum);

	fclose(fp);
}
