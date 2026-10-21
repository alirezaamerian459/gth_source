#include "gth/database/CdnUrlRepository.h"
#include "gth/database/Database.h"

namespace gth::database {

void CdnUrlRepository::insert(const gth::core::CdnUrl& cdnUrl) {
    auto stmt = db_.prepare(
        "INSERT INTO CdnUrls "
        "(cdnUrlId, url, localPath, markText, lixianUrl, isLixian, isCdn, host, capturedAt, updatedAt) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    stmt.bind(1, cdnUrl.cdnUrlId);
    stmt.bind(2, cdnUrl.url);
    stmt.bind(3, cdnUrl.localPath);
    stmt.bind(4, cdnUrl.markText);
    stmt.bind(5, cdnUrl.lixianUrl);
    stmt.bind(6, cdnUrl.isLixian ? 1 : 0);
    stmt.bind(7, cdnUrl.isCdn ? 1 : 0);
    stmt.bind(8, cdnUrl.host);
    stmt.bind(9, cdnUrl.capturedAt);
    stmt.bind(10, cdnUrl.updatedAt);
    stmt.step();
}

void CdnUrlRepository::update(const gth::core::CdnUrl& cdnUrl) {
    auto stmt = db_.prepare(
        "UPDATE CdnUrls "
        "SET url = ?, localPath = ?, markText = ?, lixianUrl = ?, isLixian = ?, "
        "isCdn = ?, host = ?, updatedAt = ? "
        "WHERE cdnUrlId = ?");
    stmt.bind(1, cdnUrl.url);
    stmt.bind(2, cdnUrl.localPath);
    stmt.bind(3, cdnUrl.markText);
    stmt.bind(4, cdnUrl.lixianUrl);
    stmt.bind(5, cdnUrl.isLixian ? 1 : 0);
    stmt.bind(6, cdnUrl.isCdn ? 1 : 0);
    stmt.bind(7, cdnUrl.host);
    stmt.bind(8, gth::core::Clock::utcNowIso8601());
    stmt.bind(9, cdnUrl.cdnUrlId);
    stmt.step();
}

void CdnUrlRepository::remove(const std::string& cdnUrlId) {
    auto stmt = db_.prepare("DELETE FROM CdnUrls WHERE cdnUrlId = ?");
    stmt.bind(1, cdnUrlId);
    stmt.step();
}

void CdnUrlRepository::removeByUrl(const std::string& url) {
    auto stmt = db_.prepare("DELETE FROM CdnUrls WHERE url = ?");
    stmt.bind(1, url);
    stmt.step();
}

std::vector<gth::core::CdnUrl> CdnUrlRepository::getAll() {
    auto stmt = db_.prepare("SELECT * FROM CdnUrls ORDER BY capturedAt DESC");
    std::vector<gth::core::CdnUrl> result;
    while (stmt.step()) {
        result.push_back(fromRow(stmt));
    }
    return result;
}

std::vector<gth::core::CdnUrl> CdnUrlRepository::getRecent(int limit) {
    auto stmt = db_.prepare("SELECT * FROM CdnUrls ORDER BY capturedAt DESC LIMIT ?");
    stmt.bind(1, limit);
    std::vector<gth::core::CdnUrl> result;
    while (stmt.step()) {
        result.push_back(fromRow(stmt));
    }
    return result;
}

gth::core::CdnUrl CdnUrlRepository::fromRow(const Statement& stmt) {
    gth::core::CdnUrl url;
    url.cdnUrlId = stmt.columnText(0);
    url.url = stmt.columnText(1);
    url.localPath = stmt.columnText(2);
    url.markText = stmt.columnText(3);
    url.lixianUrl = stmt.columnText(4);
    url.isLixian = stmt.columnBool(5);
    url.isCdn = stmt.columnBool(6);
    url.host = stmt.columnText(7);
    url.capturedAt = stmt.columnText(8);
    url.updatedAt = stmt.columnText(9);
    return url;
}

}  // namespace gth::database
