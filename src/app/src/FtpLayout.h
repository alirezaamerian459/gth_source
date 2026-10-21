#pragma once

// Geometry of the FTP tab's lower sections, shared by FtpView::OnCreate (which lays the controls out the first time)
// and FtpView::relayout... (which moves them when the admin state or the number of consoles changes), so the two can
// never disagree, and small enough to unit-test without a window.
//
// The tab, top to bottom:
//   FTP connections / Host a server / browse   (admin only)
//   Accelerator: mirror candidates              (admin only)
//   LAN Console Proxy                           (everyone - directly under the mirror list for an admin, and
//                                                directly under the page header for a signed-out user, who has none
//                                                of the above)
//   CDN URL Log                                 (everyone)
namespace ftplayout {

inline constexpr int kFieldCaptionHeight = 18;
inline constexpr int kFieldEditHeight = 24;

// LAN Console Proxy: a bold caption row, then two field rows (IP address/port/Start/Stop, then Cache Folder/Browse/
// Clear Cache), then the status dot and a few lines of status text (address to point a console at, running state,
// cache hit/miss counts).
inline constexpr int kProxyCaptionRow = 26;      // caption + the gap before the first field row's captions
inline constexpr int kProxyRowGap = 8;           // between field row 1 and field row 2
inline constexpr int kProxyFieldGap = 8;         // between the fields and the status
inline constexpr int kProxyStatusHeight = 72;    // dot + status text
inline constexpr int kProxySectionHeight = kProxyCaptionRow + kFieldCaptionHeight + kFieldEditHeight + kProxyRowGap +
                                            kFieldCaptionHeight + kFieldEditHeight + kProxyFieldGap +
                                            kProxyStatusHeight;

inline constexpr int kSectionGap = 20;  // between one section's last control and the next section's caption

}  // namespace ftplayout
