#include "catalog.hpp"

#include "binary_io.hpp"
#include "catalog_codec.hpp"
#include "crc32.hpp"

#include <algorithm>
#include <utility>

namespace lab2
{

bool is_valid_magic(const std::span<std::byte> &header)
{
    if (header.size() < 4)
    {
        return false;
    }

    if (static_cast<char>(header[0]) != 'M')
        return false;

    if (static_cast<char>(header[1]) != 'U')
        return false;

    if (static_cast<char>(header[2]) != 'S')
        return false;

    if (static_cast<char>(header[3]) != '2')
        return false;

    return true;
}

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

    if (!size)
        return {ReadStatus::InvalidOffset,
                std::nullopt,
                {},
                {},
                "Se detecto un offset invalido"};

    if (offset >= size.value())
        return {ReadStatus::InvalidOffset,
                std::nullopt,
                {},
                {},
                "Se detecto un offset invalido"};

    if (!seek_absolute(input, offset))
        return {ReadStatus::InvalidOffset,
                std::nullopt,
                {},
                {},
                "Se detecto un offset  invalido"};

    std::array<std::byte, 10> header;
    if (!read_exact(input, header))
    {
        return {ReadStatus::TruncatedHeader,
                std::nullopt,
                {},
                {},
                "Se detecto un header incompleto"};
    }

    if (!is_valid_magic(header))
        return {ReadStatus::BadMagic,
                std::nullopt,
                {},
                {},
                "Se detecto un magic incorrecto"};

    auto version_b1 = static_cast<std::uint16_t>(header[4]);
    auto version_b2 = static_cast<std::uint16_t>(header[5]);

    auto version = (version_b2 << 8) | (version_b1);

    if (version != 1)
    {
        return {ReadStatus::UnsupportedVersion,
                std::nullopt,
                {},
                {},
                "Se detecto una version sin soporte"};
    }

    auto length_b1 = static_cast<std::uint32_t>(header[6]);
    auto length_b2 = static_cast<std::uint32_t>(header[7]);
    auto length_b3 = static_cast<std::uint32_t>(header[8]);
    auto length_b4 = static_cast<std::uint32_t>(header[9]);

    auto length = (length_b4 << 24) | (length_b3 << 16) | (length_b2 << 8) |
                  (length_b1);

    if (length > MAX_PAYLOAD_SIZE || length == 0)
        return {ReadStatus::InvalidLength,
                std::nullopt,
                {},
                {},
                "se detecto una longitud invalida"};

    std::vector<std::byte> payload(length);
    if (!read_exact(input, payload))
        return {ReadStatus::TruncatedPayload,
                std::nullopt,
                {},
                {},
                "La longitud leida del payload no coincide con la real"};

    std::uint32_t crc;
    if (!read_u32_le(input, crc))
        return {ReadStatus::MissingChecksum,
                std::nullopt,
                {},
                {},
                "CRC no encontrado"};

    if (crc != crc32(payload))
        return {ReadStatus::ChecksumMismatch,
                std::nullopt,
                {},
                {},
                "El CRC leido no coincide con el calculado"};

    auto decoded_payload = decode_payload(payload);

    if (!decoded_payload.record.has_value())
        return {ReadStatus::MalformedPayload,
                std::nullopt,
                {},
                {},
                decoded_payload.detail};

    auto next_offset = offset + 10 + length + 4;

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
            return {BuildStatus::ReadError,
                    {},
                    static_cast<std::uint64_t>(offset),
                    {},
                    result.detail};
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
    (void)index;
    (void)label_id;
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

ComposerBuildResult build_composer_index(std::istream &input,
                                         std::span<const PrimaryEntry> primary)
{
    // TODO 4
    // Recomendación: reúna pares (composer, label_id), ordénelos y agrúpelos.
    // No almacene offsets en este índice secundario.
    (void)input;
    (void)primary;
    return {};
}

std::span<const std::string> find_by_composer(const ComposerIndex &index,
                                              std::string_view composer)
{
    // TODO 5
    // El ComposerIndex está ordenado por compositor: use búsqueda binaria.
    (void)index;
    (void)composer;
    return {};
}

VerificationReport verify_primary_index(std::istream &input,
                                        std::span<const PrimaryEntry> index)
{
    // TODO 6
    // Haga primero las verificaciones estructurales del índice y luego valide
    // cada referencia con read_record_at. No imprima desde esta función.
    (void)input;
    (void)index;
    return {};
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
