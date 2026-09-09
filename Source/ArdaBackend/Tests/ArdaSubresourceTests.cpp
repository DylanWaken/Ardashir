#include "RHI/ArdaRHISubresources.h"

#include <gtest/gtest.h>
#include <array>
#include <vector>

TEST(ArdaSubresources, VisitsResolvedPartialRangeInNativeOrder)
{
    arda::FArdaRHITextureDesc Desc;
    Desc.mMipLevels = 5;
    Desc.mArraySize = 4;
    Desc.mFormat = arda::EArdaRHIFormat::D24S8;
    const arda::FArdaRHITextureSubresourceRange Requested{3, 10, 2, 10, 0, 10};
    std::vector<std::array<uint32_t, 3>> Actual;
    for (const auto [Mip, Slice, Plane] : arda::FArdaTextureSubresources(Requested.Resolve(Desc)))
        Actual.push_back({Mip, Slice, Plane});
    const std::vector<std::array<uint32_t, 3>> Expected{
        {3, 2, 0}, {4, 2, 0}, {3, 3, 0}, {4, 3, 0},
        {3, 2, 1}, {4, 2, 1}, {3, 3, 1}, {4, 3, 1}};
    EXPECT_EQ(Actual, Expected);
}

TEST(ArdaSubresources, EmptyAxisNeverProducesAnElement)
{
    for (const arda::FArdaRHITextureSubresourceRange Range : {
        arda::FArdaRHITextureSubresourceRange{2, 0, 1, 2, 0, 2},
        arda::FArdaRHITextureSubresourceRange{2, 2, 1, 0, 0, 2},
        arda::FArdaRHITextureSubresourceRange{2, 2, 1, 2, 0, 0}})
    {
        for (const auto Subresource : arda::FArdaTextureSubresources(Range))
        {
            (void)Subresource;
            FAIL() << "An empty texture range must not be visited.";
        }
    }
}
