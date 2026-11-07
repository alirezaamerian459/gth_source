#include "gth/application/storage/StorageService.h"

#include <algorithm>
#include <cctype>

#include "gth/core/Clock.h"

namespace gth::application {

namespace {

std::string trimCategoryName(std::string value) {
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string foldCategoryName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

StorageCategoryResult categoryFailure(const std::string& errorMessage) {
    StorageCategoryResult result;
    result.errorMessage = errorMessage;
    return result;
}

}  // namespace

std::vector<core::StorageCategory> StorageService::getCategories() { return repository_.getCategories(); }

std::vector<std::string> StorageService::getStorageIdsForCategory(const std::string& categoryId) {
    return repository_.getStorageIdsForCategory(categoryId);
}

StorageCategoryResult StorageService::createCategory(const std::string& name) {
    const std::string cleanName = trimCategoryName(name);
    if (cleanName.empty()) return categoryFailure("Enter a category name.");

    const std::string foldedName = foldCategoryName(cleanName);
    for (const auto& category : repository_.getCategories()) {
        if (foldCategoryName(category.name) == foldedName) {
            return categoryFailure("A category with that name already exists.");
        }
    }

    core::StorageCategory category;
    category.name = cleanName;
    repository_.addCategory(category);

    StorageCategoryResult result;
    result.success = true;
    result.categoryId = category.categoryId;
    return result;
}

StorageCategoryResult StorageService::renameCategory(const std::string& categoryId, const std::string& name) {
    auto category = repository_.getCategoryById(categoryId);
    if (!category) return categoryFailure("The selected category no longer exists.");

    const std::string cleanName = trimCategoryName(name);
    if (cleanName.empty()) return categoryFailure("Enter a category name.");

    const std::string foldedName = foldCategoryName(cleanName);
    for (const auto& other : repository_.getCategories()) {
        if (other.categoryId != categoryId && foldCategoryName(other.name) == foldedName) {
            return categoryFailure("A category with that name already exists.");
        }
    }

    category->name = cleanName;
    category->updatedAt = core::Clock::utcNowIso8601();
    repository_.updateCategory(*category);

    StorageCategoryResult result;
    result.success = true;
    result.categoryId = categoryId;
    return result;
}

StorageCategoryResult StorageService::deleteCategory(const std::string& categoryId) {
    if (!repository_.getCategoryById(categoryId)) {
        return categoryFailure("The selected category no longer exists.");
    }
    repository_.removeCategory(categoryId);

    StorageCategoryResult result;
    result.success = true;
    result.categoryId = categoryId;
    return result;
}

StorageCategoryResult StorageService::addStorageToCategory(const std::string& categoryId,
                                                           const std::string& storageId) {
    if (!repository_.getCategoryById(categoryId)) return categoryFailure("Select a category first.");
    if (!repository_.getById(storageId)) return categoryFailure("The selected storage no longer exists.");
    repository_.addStorageToCategory(categoryId, storageId);

    StorageCategoryResult result;
    result.success = true;
    result.categoryId = categoryId;
    return result;
}

StorageCategoryResult StorageService::removeStorageFromCategory(const std::string& categoryId,
                                                                const std::string& storageId) {
    if (!repository_.getCategoryById(categoryId)) return categoryFailure("Select a category first.");
    if (!repository_.getById(storageId)) return categoryFailure("The selected storage no longer exists.");
    repository_.removeStorageFromCategory(categoryId, storageId);

    StorageCategoryResult result;
    result.success = true;
    result.categoryId = categoryId;
    return result;
}

}  // namespace gth::application
