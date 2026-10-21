#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "gth/core/storage/IStorageHealthProvider.h"
#include "gth/storage/SmartHealth.h"

namespace gth::storage {

// Resolves a drive letter to ALL the physical disks behind it and looks up each
// one's health/temperature separately through the standard Windows Storage WMI
// provider (root\Microsoft\Windows\Storage - MSFT_PhysicalDisk /
// MSFT_StorageReliabilityCounter), falling back to the drive's own SMART
// commands for the temperature. This is the same OS-exposed data File Explorer
// and PowerShell's Get-PhysicalDisk use - no third-party driver, nothing
// console/DRM related.
//
// Which disks a drive letter sits on:
//   * a normal volume: its one disk;
//   * a spanned / striped volume (several disks joined into one letter): every
//     disk holding an extent of it (IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS);
//   * a Storage Spaces virtual disk: the physical disks of its pool (WMI).
// A hardware RAID controller that presents its array as a single disk cannot be
// looked into from Windows - that shows up as one disk, like any other.
//
// Any failure along the way (removable media, a network share, a controller that
// doesn't expose reliability counters, WMI itself unavailable) leaves the
// corresponding field unset rather than guessing.
class WindowsStorageHealthProvider : public core::IStorageHealthProvider {
public:
    core::StorageHealthInfo queryHealth(const std::string& rootPath) override;
    std::optional<std::vector<core::PhysicalDiskHealth>> queryDisks(const std::string& rootPath) override;
};

// The distinct disk numbers named by a VOLUME_DISK_EXTENTS structure (the output of
// IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS), sorted. `bytes` is how much of `buffer` is valid;
// a truncated buffer yields an empty list rather than reading past its end.
std::vector<int> diskNumbersFromVolumeExtents(const void* buffer, std::size_t bytes);

// The disk's own SMART attribute table (with failure thresholds), read straight from \\.\PhysicalDrive<N> with the
// standard ATA SMART READ DATA / READ THRESHOLDS commands. Read-only. Needs administrator; nullopt when the disk
// isn't a SATA/ATA disk this way (NVMe, some USB bridges, RAID volumes) or can't be opened.
std::optional<std::vector<SmartAttribute>> readAtaSmartAttributes(int diskNumber);

}  // namespace gth::storage
