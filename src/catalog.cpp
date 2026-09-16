#include "catalog.hpp"

#include "binary_io.hpp"
#include "catalog_codec.hpp"
#include "crc32.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>
namespace lab2
{

const char *to_string(ReadStatus status)
{
    switch (status)
    {
    case ReadStatus::Ok:
        return "Ok";
    case ReadStatus::NotFound:
        return "NotFound";
    case ReadStatus::IndexKeyMismatch:
        return "IndexKeyMismatch";
    case ReadStatus::InvalidOffset:
        return "InvalidOffset";
    case ReadStatus::TruncatedHeader:
        return "TruncatedHeader";
    case ReadStatus::BadMagic:
        return "BadMagic";
    case ReadStatus::UnsupportedVersion:
        return "UnsupportedVersion";
    case ReadStatus::InvalidLength:
        return "InvalidLength";
    case ReadStatus::TruncatedPayload:
        return "TruncatedPayload";
    case ReadStatus::MissingChecksum:
        return "MissingChecksum";
    case ReadStatus::ChecksumMismatch:
        return "ChecksumMismatch";
    case ReadStatus::MalformedPayload:
        return "MalformedPayload";
    }
    return "UnknownReadStatus";
}

const char *to_string(BuildStatus status)
{
    switch (status)
    {
    case BuildStatus::Ok:
        return "Ok";
    case BuildStatus::ReadError:
        return "ReadError";
    case BuildStatus::DuplicateKey:
        return "DuplicateKey";
    }
    return "UnknownBuildStatus";
}

const char *to_string(VerificationIssueType type)
{
    switch (type)
    {
    case VerificationIssueType::UnsortedIndex:
        return "UnsortedIndex";
    case VerificationIssueType::DuplicateKey:
        return "DuplicateKey";
    case VerificationIssueType::DuplicateOffset:
        return "DuplicateOffset";
    case VerificationIssueType::RecordReadError:
        return "RecordReadError";
    case VerificationIssueType::KeyMismatch:
        return "KeyMismatch";
    }
    return "UnknownVerificationIssue";
}

ReadResult read_record_at(std::istream &input, std::uint64_t offset)
{
    // TODO 1
    // Orden obligatorio:
    // 1) comprobar tamaño/offset y hacer seek;
    // 2) leer magic, version y payload_length;
    // 3) validar el header ANTES de reservar memoria;
    // 4) leer payload y CRC almacenado;
    // 5) recalcular CRC;
    // 6) decodificar el payload solamente si el CRC coincide.

    auto size = stream_size(input);

    auto error = [](const ReadStatus &status, const std::string &detail)
    { return ReadResult{status, std::nullopt, {}, {}, detail}; };

    if (!size)
    {
        return error(ReadStatus::InvalidOffset,
                     "Se detecto un offset invalido");
    }

    if (offset >= size.value())
    {
        return error(ReadStatus::InvalidOffset,
                     "Se detecto un offset invalido");
    }

    if (!seek_absolute(input, offset))
    {
        return error(ReadStatus::InvalidOffset,
                     "Se detecto un offset invalido");
    }

    std::uint32_t magic;
    if (!read_u32_le(input, magic))
    {
        return error(ReadStatus::TruncatedHeader,
                     "Se detecto un header incompleto");
    }

    if (magic != RECORD_MAGIC)
    {
        return error(ReadStatus::BadMagic, "Se detecto un magic invalido");
    }

    std::uint16_t version = 0;
    if (!read_u16_le(input, version))
    {
        return error(ReadStatus::TruncatedHeader,
                     "Se detecto un header incompleto");
    }

    if (version != RECORD_VERSION)
    {
        return error(ReadStatus::UnsupportedVersion,
                     "Se detecto una version sin soporte");
    }

    std::uint32_t length = 0;
    if (!read_u32_le(input, length))
    {
        return error(ReadStatus::TruncatedHeader,
                     "Se detecto un header incompleto");
    }

    if (length > MAX_PAYLOAD_SIZE || length == 0)
    {
        return error(ReadStatus::InvalidLength,
                     "Se detecto una longitud invalida.");
    }

    std::vector<std::byte> payload(length);
    if (!read_exact(input, payload))
    {
        return error(ReadStatus::TruncatedPayload,
                     "La longitud leida del payload no coincide con la real");
    }

    std::uint32_t crc;
    if (!read_u32_le(input, crc))
    {
        return error(ReadStatus::MissingChecksum, "CRC no encontrado");
    }

    if (crc != crc32(payload))
    {
        return error(ReadStatus::ChecksumMismatch,
                     "El CRC calculado no coincide con el esperado");
    }

    auto decoded_payload = decode_payload(payload);

    if (!decoded_payload.record.has_value())
    {
        return error(ReadStatus::MalformedPayload, decoded_payload.detail);
    }

    auto next_offset =
            offset + RECORD_HEADER_SIZE + length + RECORD_CHECKSUM_SIZE;

    return {ReadStatus::Ok,
            decoded_payload.record,
            offset,
            next_offset,
            "el registro se pudo leer correctamente"};
}

PrimaryBuildResult build_primary_index(std::istream &input)
{
    // TODO 2
    // Recorra el archivo con next_offset, ordene por label_id y detecte
    // duplicados.

    std::uint64_t offset = 0;
    std::vector<PrimaryEntry> entries;

    auto size = stream_size(input);

    if (!size)
    {
        return {BuildStatus::ReadError,
                {},
                {},
                {},
                "No se pudo determinar el tamanio del archivo"};
    }

    while (true)
    {
        if (offset == size.value())
            break;

        ReadResult result = read_record_at(input, offset);

        if (result.status != ReadStatus::Ok)
        {
            return {BuildStatus::ReadError, {}, offset, {}, result.detail};
        }

        auto result_record = result.record.value();

        entries.emplace_back(result_record.label_id, offset);

        offset = result.next_offset;
    }

    std::sort(entries.begin(),
              entries.end(),
              [](const PrimaryEntry &a, const PrimaryEntry &b)
              { return a.label_id < b.label_id; });

    for (std::size_t i = 1; i < entries.size(); i++)
    {
        if (entries[i].label_id == entries[i - 1].label_id)
        {
            return {BuildStatus::DuplicateKey,
                    {},
                    {},
                    entries[i].label_id,
                    "Se detecto una clave repetida"};
        }
    }

    return {BuildStatus::Ok,
            entries,
            {},
            {},
            "El indice primario se construyo correctamente"};
}

std::optional<std::uint64_t> find_offset(std::span<const PrimaryEntry> index,
                                         std::string_view label_id)
{
    // TODO 3
    // Implemente búsqueda binaria manual. No use std::lower_bound,
    // std::binary_search ni std::equal_range.

    if (index.empty())
        return std::nullopt;

    int left = 0;
    int right = index.size() - 1;
    int middle = right / 2;

    while (left <= right)
    {
        std::size_t middle_idx = static_cast<std::size_t>(middle);
        if (index[middle_idx].label_id == label_id)
        {
            return index[middle_idx].offset;
        }

        if (index[middle_idx].label_id < label_id)
        {
            left = middle + 1;
            middle = left + ((right - left) / 2);
            continue;
        }

        if (index[middle_idx].label_id > label_id)
        {
            right = middle - 1;
            middle = left + ((right - left) / 2);
            continue;
        }
    }

    return std::nullopt;
}

ReadResult find_record(std::istream &input,
                       std::span<const PrimaryEntry> index,
                       std::string_view label_id)
{
    const auto offset = find_offset(index, label_id);
    if (!offset.has_value())
    {
        return {ReadStatus::NotFound,
                std::nullopt,
                0,
                0,
                "La clave no existe en el índice primario."};
    }
    ReadResult result = read_record_at(input, *offset);
    if (result.ok() && result.record->label_id != label_id)
    {
        result.status = ReadStatus::IndexKeyMismatch;
        result.record.reset();
        result.detail =
                "La clave del índice no coincide con la clave del registro.";
    }
    return result;
}
using ComposerLabelPair = std::pair<std::string, std::string>;

ComposerBuildResult build_composer_index(std::istream &input,
                                         std::span<const PrimaryEntry> primary)
{
    std::vector<SkippedRecord> skipped;
    std::vector<ComposerLabelPair> composers_labels;
    for (const PrimaryEntry &prim : primary)
    {
        ReadResult result = read_record_at(input, prim.offset);
        if (!result.ok())
        {
            skipped.emplace_back(prim.label_id, prim.offset, result.status);
            continue;
        }

        Record rec = result.record.value();

        if (rec.label_id != prim.label_id)
        {
            skipped.emplace_back(
                    prim.label_id, prim.offset, ReadStatus::IndexKeyMismatch);
            continue;
        }

        composers_labels.emplace_back(rec.composer, rec.label_id);
    }
    std::sort(composers_labels.begin(), composers_labels.end());

    ComposerIndex composer_index;
    std::vector<std::string> labels_id;
    std::string current_composer;
    if (!composers_labels.empty())
    {
        current_composer = composers_labels[0].first;
        labels_id.emplace_back(composers_labels[0].second);
    }

    for (std::size_t i = 1; i < composers_labels.size(); i++)
    {
        if (composers_labels[i].first != current_composer)
        {
            composer_index.emplace_back(current_composer, labels_id);
            current_composer = composers_labels[i].first;
            labels_id.clear();
            labels_id.emplace_back(composers_labels[i].second);
            continue;
        }

        if (composers_labels[i].second == composers_labels[i - 1].second)
            continue;

        labels_id.emplace_back(composers_labels[i].second);
    }

    if (!composers_labels.empty())
        composer_index.emplace_back(current_composer, labels_id);

    std::sort(composer_index.begin(),
              composer_index.end(),
              [](const ComposerEntry &a, const ComposerEntry &b)
              { return a.composer < b.composer; });

    return {composer_index, skipped};
}

std::span<const std::string> find_by_composer(const ComposerIndex &index,
                                              std::string_view composer)
{
    if (index.empty())
        return {};

    int left = 0;
    int right = index.size() - 1;
    int middle = right / 2;

    while (left <= right)
    {
        std::size_t middle_idx = static_cast<std::size_t>(middle);
        if (index[middle_idx].composer == composer)
        {
            return index[middle_idx].label_ids;
        }

        if (index[middle_idx].composer < composer)
        {
            left = middle + 1;
            middle = left + ((right - left) / 2);
            continue;
        }

        if (index[middle_idx].composer > composer)
        {
            right = middle - 1;
            middle = left + ((right - left) / 2);
            continue;
        }
    }
    return {};
}

VerificationReport verify_primary_index(std::istream &input,
                                        std::span<const PrimaryEntry> index)
{
    // TODO 6
    // Haga primero las verificaciones estructurales del índice y luego valide
    // cada referencia con read_record_at. No imprima desde esta función.

    std::vector<VerificationIssue> issues;

    std::size_t entries_checked = 0;
    std::size_t readable_matching_entries = 0;

    std::unordered_set<std::string> seen_keys;
    std::unordered_set<std::uint64_t> seen_offsets;

    for (std::size_t i = 0; i < index.size(); i++)
    {
        if ((i > 0) && (index[i].label_id < index[i - 1].label_id))
        {
            issues.emplace_back(
                    VerificationIssueType::UnsortedIndex,
                    index[i].label_id,
                    index[i].offset,
                    ReadStatus::Ok,
                    "El indice no esta ordenado de forma ascendente.");
        }

        if (!seen_keys.insert(index[i].label_id).second)
        {
            issues.emplace_back(
                    VerificationIssueType::DuplicateKey,
                    index[i].label_id,
                    index[i].offset,
                    ReadStatus::Ok,
                    "Se encontro una llave duplicada en el indice.");
        }

        if (!seen_offsets.insert(index[i].offset).second)
        {
            issues.emplace_back(
                    VerificationIssueType::DuplicateOffset,
                    index[i].label_id,
                    index[i].offset,
                    ReadStatus::Ok,
                    "Se encontro un offset duplicado en el indice.");
        }
    }

    for (const PrimaryEntry &entry : index)
    {
        entries_checked++;

        ReadResult read_result = read_record_at(input, entry.offset);

        if (!read_result.ok())
        {
            issues.emplace_back(VerificationIssueType::RecordReadError,
                                entry.label_id,
                                entry.offset,
                                read_result.status,
                                "No se puedo leer la clave.");
        }
        else if (read_result.record->label_id != entry.label_id)
        {
            issues.emplace_back(VerificationIssueType::KeyMismatch,
                                entry.label_id,
                                entry.offset,
                                read_result.status,
                                "La clave leida no coincide con la esperada.");
        }
        else
        {
            readable_matching_entries++;
        }
    }
    return {entries_checked, readable_matching_entries, issues};
}

std::vector<std::string> intersect_sorted(std::span<const std::string> left,
                                          std::span<const std::string> right)
{
    // TODO BONO: dos punteros, O(n + m), sin duplicados.
    (void)left;
    (void)right;
    return {};
}

} // namespace lab2
