// Known-answer tests for CLK's media layer, driven through the static analyser's public entry points.
// Built by mayhem/build.sh with the project's normal flags and run by mayhem/test.sh, which parses the
// final KAT_SUMMARY line.
#include "Analyser/Static/StaticAnalyser.hpp"
#include "Analyser/Static/Atari2600/Target.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

int total = 0, failed = 0;

void expect(const char *name, bool condition) {
	++total;
	if(!condition) ++failed;
	std::printf("%s %s\n", condition ? "PASS" : "FAIL", name);
}

std::filesystem::path write_file(const std::filesystem::path &directory, const char *name, const std::vector<uint8_t> &data) {
	const auto path = directory / name;
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
	return path;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

void test_spectrum_tap(const std::filesystem::path &dir) {
	// One block of two bytes: flag 0x00 (a header block) and a zero payload byte.
	const auto path = write_file(dir, "block.tap", {0x02, 0x00, 0x00, 0x00});
	const auto targets = Analyser::Static::GetTargets(path);
	expect("tap: one target", targets.size() == 1);
	if(targets.size() != 1) return;
	expect("tap: machine is ZX Spectrum", targets[0]->machine == Analyser::Machine::ZXSpectrum);
	expect("tap: one tape", targets[0]->media.tapes.size() == 1);
	if(targets[0]->media.tapes.size() != 1) return;

	const auto serialiser = targets[0]->media.tapes[0]->serialiser();
	Storage::Tape::Pulse pulse;
	std::vector<Storage::Tape::Pulse> pulses;
	for(int c = 0; c < 3224; c++) pulses.push_back(serialiser->next_pulse());
	expect("tap: pilot pulse is 2168 T-states", near(pulses[0].length.as<double>(), 2168.0 / 3500000.0));
	expect("tap: pilot pulse is the same on pulse 3222", near(pulses[3221].length.as<double>(), 2168.0 / 3500000.0));
	expect("tap: first sync pulse is 667 T-states", near(pulses[3222].length.as<double>(), 667.0 / 3500000.0));
	expect("tap: second sync pulse is 735 T-states", near(pulses[3223].length.as<double>(), 735.0 / 3500000.0));
	expect("tap: pulses alternate polarity", pulses[0].type != pulses[1].type);
	expect("tap: not at end after sync", !serialiser->is_at_end());
}

void test_spectrum_tap_rejects_truncated(const std::filesystem::path &dir) {
	// Declares 16 bytes of block; supplies 1.
	const auto path = write_file(dir, "short.tap", {0x10, 0x00, 0x00});
	const auto media = Analyser::Static::GetMedia(path);
	expect("tap: truncated block is rejected", media.empty());
}

void test_commodore_tap(const std::filesystem::path &dir) {
	std::vector<uint8_t> tap = {'C', '6', '4', '-', 'T', 'A', 'P', 'E', '-', 'R', 'A', 'W', 0x00, 0x01, 0x00, 0x00,
		0x03, 0x00, 0x00, 0x00, 0x30, 0x40, 0x50};
	const auto path = write_file(dir, "c64.tap", tap);
	const auto media = Analyser::Static::GetMedia(path);
	expect("commodore tap: one tape", media.tapes.size() == 1);
	if(media.tapes.size() != 1) return;
	const auto serialiser = media.tapes[0]->serialiser();
	std::vector<double> lengths;
	for(int c = 0; c < 6 && !serialiser->is_at_end(); c++) lengths.push_back(serialiser->next_pulse().length.as<double>());
	// Each byte is a pair of half-pulses of (byte * 8) / 2 PAL C64 clock cycles.
	expect("commodore tap: six half-pulses", lengths.size() == 6);
	if(lengths.size() != 6) return;
	expect("commodore tap: 0x30 -> 192 cycles", near(lengths[0], 192.0 / 985248.0) && near(lengths[1], 192.0 / 985248.0));
	expect("commodore tap: 0x40 -> 256 cycles", near(lengths[2], 256.0 / 985248.0) && near(lengths[3], 256.0 / 985248.0));
	expect("commodore tap: 0x50 -> 320 cycles", near(lengths[4], 320.0 / 985248.0) && near(lengths[5], 320.0 / 985248.0));
}

void test_atari_rom(const std::filesystem::path &dir) {
	std::vector<uint8_t> rom(4096, 0xea);
	const auto path = write_file(dir, "game.a26", rom);
	const auto targets = Analyser::Static::GetTargets(path);
	expect("a26: one target", targets.size() == 1);
	if(targets.size() != 1) return;
	expect("a26: machine is Atari 2600", targets[0]->machine == Analyser::Machine::Atari2600);
	const auto &target = static_cast<const Analyser::Static::Atari2600::Target &>(*targets[0]);
	expect("a26: 4kb needs no paging", target.paging_model == Analyser::Static::Atari2600::Target::PagingModel::None);
	expect("a26: cartridge present", targets[0]->media.cartridges.size() == 1);
	if(targets[0]->media.cartridges.size() == 1) {
		const auto &segments = targets[0]->media.cartridges[0]->segments();
		expect("a26: one segment of 4096 bytes", segments.size() == 1 && segments[0].data.size() == 4096);
	}

	rom.assign(8192, 0xea);
	const auto path8 = write_file(dir, "game8.a26", rom);
	const auto targets8 = Analyser::Static::GetTargets(path8);
	expect("a26 8k: one target", targets8.size() == 1);
	if(targets8.size() == 1) {
		const auto &target8 = static_cast<const Analyser::Static::Atari2600::Target &>(*targets8[0]);
		expect("a26 8k: Atari8k paging", target8.paging_model == Analyser::Static::Atari2600::Target::PagingModel::Atari8k);
	}
}

void test_cartridge_dump(const std::filesystem::path &dir) {
	std::vector<uint8_t> rom(16384);
	for(size_t c = 0; c < rom.size(); c++) rom[c] = uint8_t((c * 7 + 3) & 0xff);
	const auto path = write_file(dir, "cart.rom", rom);
	const auto media = Analyser::Static::GetMedia(path);
	expect("rom: one cartridge", media.cartridges.size() == 1);
	if(media.cartridges.size() != 1) return;
	const auto &segments = media.cartridges[0]->segments();
	expect("rom: segment holds the file verbatim", segments.size() == 1 && segments[0].data == rom);
}

void test_ssd(const std::filesystem::path &dir) {
	std::vector<uint8_t> image(20 * 10 * 256);
	for(size_t c = 0; c < image.size(); c++) image[c] = uint8_t(c);
	const auto path = write_file(dir, "disk.ssd", image);
	const auto media = Analyser::Static::GetMedia(path);
	expect("ssd: one disk", media.disks.size() == 1);
	if(media.disks.size() != 1) return;
	auto &disk = *media.disks[0];
	expect("ssd: single sided", disk.head_count() == 1);
	expect("ssd: short image is padded to 40 tracks", disk.maximum_head_position().as_int() == 40);

	auto track = disk.track_at_position(Storage::Disk::Track::Address(0, Storage::Disk::HeadPosition(0)));
	expect("ssd: track 0 exists", track != nullptr);
	if(!track) return;
	int flux = 0;
	double sum = 0;
	for(;;) {
		const auto event = track->get_next_event();
		sum += event.length.as<double>();
		if(event.type == Storage::Disk::Track::Event::IndexHole) break;
		++flux;
		if(flux > 1000000) break;
	}
	expect("ssd: track 0 holds 39240 flux transitions", flux == 39240);
	expect("ssd: track 0 spans exactly one revolution", near(sum, 1.0));
}

void test_d64(const std::filesystem::path &dir) {
	std::vector<uint8_t> image(174848, 0);
	const auto path = write_file(dir, "disk.d64", image);
	const auto media = Analyser::Static::GetMedia(path);
	expect("d64: one disk", media.disks.size() == 1);
	if(media.disks.size() != 1) return;
	expect("d64: single sided", media.disks[0]->head_count() == 1);
	expect("d64: 35 tracks", media.disks[0]->maximum_head_position().as_int() == 35);
}

void test_bad_input(const std::filesystem::path &dir) {
	const auto path = write_file(dir, "junk.d64", {1, 2, 3});
	expect("d64: wrong-size image is rejected", Analyser::Static::GetMedia(path).empty());
	const auto unknown = write_file(dir, "thing.xyz", {1, 2, 3});
	expect("unknown extension yields no targets", Analyser::Static::GetTargets(unknown).empty());
}

}

int main() {
	char pattern[] = "/tmp/clkkatXXXXXX";
	if(!mkdtemp(pattern)) return 2;
	const std::filesystem::path dir(pattern);

	test_spectrum_tap(dir);
	test_spectrum_tap_rejects_truncated(dir);
	test_commodore_tap(dir);
	test_atari_rom(dir);
	test_cartridge_dump(dir);
	test_ssd(dir);
	test_d64(dir);
	test_bad_input(dir);

	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::printf("KAT_SUMMARY: %d run; %d failed\n", total, failed);
	return failed ? 1 : 0;
}
