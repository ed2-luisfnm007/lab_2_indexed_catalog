#include "doctest/doctest.h"

#include "binary_io.hpp"
#include "catalog.hpp"
#include "catalog_codec.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

// Agregue aquí al menos tres casos de prueba propios.
// No defina DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN en este archivo.

// Ejemplo de estructura (reemplace o elimine este comentario):
// TEST_CASE("Mi prueba - descripción precisa") {
//     CHECK(/* condición */);
// }

TEST_CASE("ST1 - build_primary_index con archivo vacio produce indice vacio")
{
    std::stringstream input(std::string(),
                            std::ios::binary | std::ios::in | std::ios::out);

    const lab2::PrimaryBuildResult result = lab2::build_primary_index(input);

    CHECK(result.status == lab2::BuildStatus::Ok);
    CHECK(result.entries.empty());
}

TEST_CASE("ST2 - read_record_at con longitud invalida produce InvalidLength")
{
    std::stringstream out(std::ios::binary | std::ios::out | std::ios::in);
    REQUIRE(out.write("MUS2", 4));
    REQUIRE(lab2::write_u16_le(out, 1));
    REQUIRE(lab2::write_u32_le(out, std::numeric_limits<std::uint32_t>::max()));

    const auto result = lab2::read_record_at(out, 0);

    CHECK(result.status == lab2::ReadStatus::InvalidLength);
    CHECK_FALSE(result.record.has_value());
}

TEST_CASE("ST3 - verify_primary_index con indice desordenado y llave duplicada "
          "produce DuplicateKey y UnsortedIndex")
{
    std::vector<lab2::PrimaryEntry> index = {
            {"B", 0}, {"A", 10}, {"E", 20}, {"C", 30}, {"A", 40}};
    std::stringstream input(std::string(),
                            std::ios::binary | std::ios::in | std::ios::out);

    const lab2::VerificationReport result =
            lab2::verify_primary_index(input, index);
    const std::vector<lab2::VerificationIssue> issues = result.issues;

    bool is_unsorted = std::any_of(
            issues.begin(),
            issues.end(),
            [](const lab2::VerificationIssue &issue)
            {
                return issue.type == lab2::VerificationIssueType::UnsortedIndex;
            });
    bool is_duplicate = std::any_of(
            issues.begin(),
            issues.end(),
            [](const lab2::VerificationIssue &issue)
            {
                return issue.type == lab2::VerificationIssueType::DuplicateKey;
            });

    CHECK(is_unsorted);
    CHECK(is_duplicate);
}