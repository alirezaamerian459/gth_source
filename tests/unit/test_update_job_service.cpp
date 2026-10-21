#include <gtest/gtest.h>

#include "gth/application/updates/UpdateJobService.h"
#include "gth/database/Database.h"
#include "gth/database/Migrator.h"
#include "gth/database/UpdateJobRepository.h"

using namespace gth::core;
using namespace gth::database;
using namespace gth::application;

namespace {
struct JobFixture {
    Database db{":memory:"};
    UpdateJobRepository jobRepository{db};
    UpdateJobService service{jobRepository};

    JobFixture() { Migrator::applyMigrations(db); }
};
}  // namespace

TEST(UpdateJobServiceTest, QueueUpdateCreatesANewJob) {
    JobFixture fixture;
    auto job = fixture.service.queueUpdate("variant-1", "1.04", "1.06");

    EXPECT_EQ(job.status, UpdateJobStatus::Queued);
    EXPECT_EQ(job.oldVersion, "1.04");
    EXPECT_EQ(job.newVersion, "1.06");
    EXPECT_EQ(fixture.jobRepository.getByVariantId("variant-1").size(), 1u);
}

TEST(UpdateJobServiceTest, QueueingTheSameUpdateTwiceDoesNotCreateADuplicate) {
    JobFixture fixture;
    auto first = fixture.service.queueUpdate("variant-1", "1.04", "1.06");
    auto second = fixture.service.queueUpdate("variant-1", "1.04", "1.06");

    EXPECT_EQ(first.updateJobId, second.updateJobId);
    EXPECT_EQ(fixture.jobRepository.getByVariantId("variant-1").size(), 1u);
}

TEST(UpdateJobServiceTest, QueueingAfterPreviousJobCompletedCreatesANewOne) {
    JobFixture fixture;
    auto first = fixture.service.queueUpdate("variant-1", "1.04", "1.06");

    first.status = UpdateJobStatus::Completed;
    first.updatedAt = Clock::utcNowIso8601();
    fixture.jobRepository.update(first);

    auto second = fixture.service.queueUpdate("variant-1", "1.04", "1.06");

    EXPECT_NE(first.updateJobId, second.updateJobId);
    EXPECT_EQ(fixture.jobRepository.getByVariantId("variant-1").size(), 2u);
}

TEST(UpdateJobServiceTest, DifferentTargetVersionIsNotADuplicate) {
    JobFixture fixture;
    fixture.service.queueUpdate("variant-1", "1.04", "1.05");
    fixture.service.queueUpdate("variant-1", "1.05", "1.06");

    EXPECT_EQ(fixture.jobRepository.getByVariantId("variant-1").size(), 2u);
}
