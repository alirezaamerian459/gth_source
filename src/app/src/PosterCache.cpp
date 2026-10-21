#include "PosterCache.h"

#include <shlwapi.h>
#include <gdiplus.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

#include "Theme.h"
#include "gth/infrastructure/ServiceContainer.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

namespace {
constexpr int kCornerRadius = 14;

Gdiplus::Color ToGdiplusColor(COLORREF c) {
    return Gdiplus::Color(GetRValue(c), GetGValue(c), GetBValue(c));
}

void AddRoundedRectPath(Gdiplus::GraphicsPath& path, int width, int height, int radius) {
    const int d = radius * 2;
    path.AddArc(0, 0, d, d, 180, 90);
    path.AddArc(width - d, 0, d, d, 270, 90);
    path.AddArc(width - d, height - d, d, d, 0, 90);
    path.AddArc(0, height - d, d, d, 90, 90);
    path.CloseFigure();
}

// Every tile this cache hands out is built the same way: a top-down 32bpp DIB section (so the
// resulting HBITMAP is directly usable with AlphaBlend/an ILC_COLOR32 image list - no separate
// "flatten alpha against a background color" step, unlike Gdiplus::Bitmap::GetHBITMAP), wrapped
// in a Gdiplus::Bitmap constructed *over* that same memory so drawing through it writes straight
// into the DIB, then clipped to a rounded rect before the caller draws its actual content.
HBITMAP RenderRoundedTile(const std::function<void(Gdiplus::Graphics&)>& drawContent) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = PosterCache::kTileWidth;
    bmi.bmiHeader.biHeight = -PosterCache::kTileHeight;  // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP hbitmap = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbitmap || !bits) return nullptr;

    {
        Gdiplus::Bitmap canvas(PosterCache::kTileWidth, PosterCache::kTileHeight, PosterCache::kTileWidth * 4,
                                PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::GraphicsPath path;
        AddRoundedRectPath(path, PosterCache::kTileWidth, PosterCache::kTileHeight, kCornerRadius);
        g.SetClip(&path);

        drawContent(g);
    }
    return hbitmap;
}
}  // namespace

PosterCache::PosterCache() {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr);
    cacheDir_ = (gth::infrastructure::ServiceContainer::defaultDataDirectory() / "Covers").wstring();
    std::error_code ec;
    std::filesystem::create_directories(cacheDir_, ec);
}

PosterCache::~PosterCache() {
    // A request already in flight isn't cancelled - WinHTTP has no clean way to do that without
    // closing its handle from this thread while the worker is blocked inside it, which is more
    // complexity than a rare few-second shutdown delay justifies (the same tradeoff UpdatesView's
    // own background threads already accept - see its OnDestroy). Worst case is one request's
    // timeout (a few seconds), not a hang: the cover-art client's WinHTTP session has a fixed timeout
    // on every call.
    running_.store(false);
    queueCv_.notify_one();
    if (worker_.joinable()) worker_.join();

    // Tile bitmaps are deliberately never DeleteObject'd here - the same "intentionally leaked for
    // the process lifetime" convention Theme.h's fonts/brushes already use (see its own comment on
    // that). The alternative would need to track which bitmaps are a real, single-owner poster
    // versus a placeholder shared across every titleId of the same platform (byTitleId_ can hold
    // the same HBITMAP under several keys), just to avoid double-deleting a shared one - real
    // complexity for cleanup the OS already does for free the moment this process exits, since
    // PosterCache lives exactly as long as the app does.
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

std::string PosterCache::readDiskCache(const std::string& titleId) const {
    const std::wstring path = cacheDir_ + L"\\" + std::wstring(titleId.begin(), titleId.end()) + L".png";
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void PosterCache::writeDiskCache(const std::string& titleId, const std::string& pngBytes) const {
    const std::wstring path = cacheDir_ + L"\\" + std::wstring(titleId.begin(), titleId.end()) + L".png";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out.is_open()) out.write(pngBytes.data(), static_cast<std::streamsize>(pngBytes.size()));
}

HBITMAP PosterCache::buildFromPng(const std::string& pngBytes) {
    IStream* stream = ::SHCreateMemStream(reinterpret_cast<const BYTE*>(pngBytes.data()),
                                           static_cast<UINT>(pngBytes.size()));
    if (!stream) return nullptr;

    HBITMAP result = nullptr;
    {
        Gdiplus::Bitmap source(stream);
        if (source.GetLastStatus() == Gdiplus::Ok) {
            result = RenderRoundedTile([&source](Gdiplus::Graphics& g) {
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(&source, 0, 0, PosterCache::kTileWidth, PosterCache::kTileHeight);
            });
        }
    }
    stream->Release();
    return result;
}

HBITMAP PosterCache::placeholderFor(const std::string& platform) {
    if (const auto it = placeholdersByPlatform_.find(platform); it != placeholdersByPlatform_.end()) {
        return it->second;
    }

    const std::wstring label(platform.begin(), platform.end());
    HBITMAP bitmap = RenderRoundedTile([&](Gdiplus::Graphics& g) {
        g.Clear(ToGdiplusColor(theme::HoverBg()));

        Gdiplus::Pen border(ToGdiplusColor(theme::Border()), 1.0f);
        g.DrawRectangle(&border, 1, 1, kTileWidth - 2, kTileHeight - 2);

        Gdiplus::FontFamily family(L"Segoe UI");
        Gdiplus::Font font(&family, 15.0f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush textBrush(ToGdiplusColor(theme::TextSecondary()));
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layout(0, 0, static_cast<Gdiplus::REAL>(kTileWidth), static_cast<Gdiplus::REAL>(kTileHeight));
        g.DrawString(label.empty() ? L"?" : label.c_str(), -1, &font, layout, &format, &textBrush);
    });

    placeholdersByPlatform_[platform] = bitmap;
    return bitmap;
}

void PosterCache::enableWebFallback(gth::core::ICoverArtClient& client) {
    webClient_ = &client;
    running_.store(true);
    worker_ = std::thread([this] { workerLoop(); });
}

void PosterCache::subscribe(HWND hwnd) { subscribers_.push_back(hwnd); }

void PosterCache::drainPendingWebResults() {
    std::vector<std::pair<std::string, std::string>> drained;
    {
        std::lock_guard<std::mutex> lock(resultsMutex_);
        if (pendingResults_.empty()) return;
        drained.swap(pendingResults_);
    }
    for (auto& [titleId, pngBytes] : drained) {
        if (HBITMAP bitmap = buildFromPng(pngBytes)) byTitleId_[titleId] = bitmap;
    }
}

void PosterCache::queueWebFetch(const std::string& titleId, const std::string& title) {
    if (!webClient_ || title.empty() || requestedTitleIds_.contains(titleId)) return;
    requestedTitleIds_.insert(titleId);
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        requestQueue_.push_back({titleId, title});
    }
    queueCv_.notify_one();
}

void PosterCache::workerLoop() {
    while (running_.load()) {
        WebRequest request;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [this] { return !requestQueue_.empty() || !running_.load(); });
            if (!running_.load()) return;
            request = std::move(requestQueue_.front());
            requestQueue_.pop_front();
        }

        auto pngBytes = webClient_->fetchPosterByTitle(request.title);
        if (!pngBytes || pngBytes->empty()) continue;

        writeDiskCache(request.titleId, *pngBytes);
        {
            std::lock_guard<std::mutex> lock(resultsMutex_);
            pendingResults_.emplace_back(request.titleId, std::move(*pngBytes));
        }
        for (HWND hwnd : subscribers_) ::PostMessageW(hwnd, kPosterReadyMessage, 0, 0);
    }
}

HBITMAP PosterCache::posterFor(const std::string& titleId, const std::string& title, const std::string& platform,
                                gth::application::IGamePosterService* posterService,
                                const std::string& gameVariantId) {
    drainPendingWebResults();

    if (titleId.empty()) return placeholderFor(platform);

    if (const auto it = byTitleId_.find(titleId); it != byTitleId_.end()) return it->second;

    std::string pngBytes = readDiskCache(titleId);
    bool fromDisk = !pngBytes.empty();
    if (pngBytes.empty() && posterService) {
        if (auto extracted = posterService->extractPosterPng(gameVariantId)) pngBytes = std::move(*extracted);
    }

    HBITMAP bitmap = nullptr;
    if (!pngBytes.empty()) {
        bitmap = buildFromPng(pngBytes);
        if (bitmap && !fromDisk) writeDiskCache(titleId, pngBytes);
    }
    if (bitmap) {
        byTitleId_[titleId] = bitmap;
        return bitmap;
    }

    // Deliberately NOT memoized into byTitleId_: unlike a resolved poster (real art, or a titleId
    // with nothing left to try), this is "nothing worked *this* call" - the drive could come back
    // online, a rescan could add the game's package files, or (once queueWebFetch's own one-time
    // dedup lets it) a web fetch could still be running. Returning the placeholder here without
    // caching it means the very next posterFor() call for this titleId - the next refresh() - just
    // tries local extraction again instead of being stuck on a placeholder for the rest of the
    // session.
    queueWebFetch(titleId, title);
    return placeholderFor(platform);
}
