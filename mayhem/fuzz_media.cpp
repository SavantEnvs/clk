// Fuzz harness for CLK's media layer: the static analyser (Analyser::Static::GetTargets /
// GetMedia) and the disk, tape, cartridge, mass-storage and snapshot readers behind it.
//
// The analyser dispatches on a file's extension, so the first input byte selects an extension
// from the list for this binary's class and the rest is the file body. Each input is staged in a
// private scratch directory under $TMPDIR. Built three times via -DCLASS_DISK / -DCLASS_TAPE /
// -DCLASS_OTHER.
#include "Analyser/Static/StaticAnalyser.hpp"
#include "Storage/Disk/DiskImage/Formats/HFE.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include <unistd.h>

namespace {

#if defined(CLASS_DISK)
constexpr const char *kExtensions[] = {
	".2mg", ".adf", ".adl", ".d64", ".dmk", ".do", ".dsk", ".dsd", ".fd", ".g64", ".ima", ".image", ".imd",
	".img", ".ipf", ".jfd", ".moof", ".msa", ".nib", ".po", ".sap", ".ssd", ".st", ".stx", ".woz",
	// GetTargets' HFE entry is registered under "hfe" (no dot) so never matches; ".hfe" is opened directly below.
	".hfe",
};
#elif defined(CLASS_TAPE)
constexpr const char *kExtensions[] = {
	".80", ".81", ".o", ".p", ".p81", ".cas", ".cdt", ".csw", ".k5", ".k7", ".lep", ".prg", ".tap", ".tsx",
	".tzx", ".uef",
};
#elif defined(CLASS_OTHER)
constexpr const char *kExtensions[] = {
	".a26", ".bin", ".ccc", ".col", ".m5", ".mx2", ".rom", ".sg", ".sms", ".hdv", ".dat", ".vhd", ".sna", ".szx",
	".z80", ".bas", ".com", ".prg",
};
#else
#error "define one of CLASS_DISK, CLASS_TAPE, CLASS_OTHER"
#endif

constexpr size_t kExtensionCount = sizeof(kExtensions) / sizeof(*kExtensions);
constexpr size_t kMaxInput = 1 << 20;
constexpr int kMaxEvents = 2048;
constexpr size_t kMaxBlocks = 64;

void walk_disk(Storage::Disk::Disk &disk) {
	const int heads = disk.head_count();
	const int last = disk.maximum_head_position().as_int();
	if(heads < 0 || heads > 4 || last < 0 || last > 256) return;
	for(int head = 0; head < heads; head++) {
		for(int position = 0; position < last; position++) {
			auto track = disk.track_at_position(Storage::Disk::Track::Address(head, Storage::Disk::HeadPosition(position)));
			if(!track) continue;
			for(int event = 0; event < kMaxEvents; event++) {
				const auto next = track->get_next_event();
				if(next.type == Storage::Disk::Track::Event::IndexHole) break;
			}
		}
	}
}

void walk_tape(Storage::Tape::Tape &tape) {
	const auto serialiser = tape.serialiser();
	if(!serialiser) return;
	for(int pulse = 0; pulse < kMaxEvents && !serialiser->is_at_end(); pulse++) {
		serialiser->next_pulse();
	}
}

void walk_mass_storage(Storage::MassStorage::MassStorageDevice &device) {
	const size_t blocks = device.get_number_of_blocks();
	for(size_t block = 0; block < blocks && block < kMaxBlocks; block++) {
		device.get_block(block);
	}
	if(blocks > kMaxBlocks) device.get_block(blocks - 1);
}

void walk_media(const Analyser::Static::Media &media) {
	for(const auto &disk : media.disks) walk_disk(*disk);
	for(const auto &tape : media.tapes) walk_tape(*tape);
	for(const auto &device : media.mass_storage_devices) walk_mass_storage(*device);
	for(const auto &cartridge : media.cartridges) {
		for(const auto &segment : cartridge->segments()) (void)segment.data.size();
	}
}

struct Scratch {
	explicit Scratch(const uint8_t *data, size_t size, const char *extension) {
		const char *tmp = std::getenv("TMPDIR");
		std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp") + "/clkfuzzXXXXXX";
		if(!mkdtemp(pattern.data())) return;
		directory = pattern;
		path = directory / (std::string("input") + extension);
		std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(data), std::streamsize(size));
		valid = true;
	}
	~Scratch() {
		std::error_code ec;
		if(!directory.empty()) std::filesystem::remove_all(directory, ec);
	}
	std::filesystem::path directory, path;
	bool valid = false;
};

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if(size < 2 || size > kMaxInput) return -1;
	const char *extension = kExtensions[data[0] % kExtensionCount];
	Scratch scratch(data + 1, size - 1, extension);
	if(!scratch.valid) return -1;

	const auto targets = Analyser::Static::GetTargets(scratch.path);
	for(const auto &target : targets) walk_media(target->media);

	walk_media(Analyser::Static::GetMedia(scratch.path));

#if defined(CLASS_DISK)
	if(std::string_view(extension) == ".hfe") {
		try {
			Storage::Disk::DiskImageHolder<Storage::Disk::HFE> hfe(scratch.path);
			walk_disk(hfe);
		} catch(...) {}
	}
#endif
	return 0;
}
