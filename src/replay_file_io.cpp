#include "replay_file_io.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <utility>

namespace forevertas {
namespace {

forevervalidator::ValidationError ReplayReadError(
        forevervalidator::ValidationErrorCategory category,
        forevervalidator::ValidationErrorCode code,
        forevervalidator::ValidationFailureReason reason,
        const forevervalidator::ReplayIdentity &identity,
        const std::string &path,
        const char *diagnostic) {
    forevervalidator::ValidationError error;
    error.category = category;
    error.code = code;
    error.stage = forevervalidator::ValidationStage::ReplayDecoding;
    error.reason = reason;
    error.replay = identity;
    error.relatedAsset = path;
    error.diagnostic = diagnostic;
    return error;
}

}  // namespace

forevervalidator::Result<forevervalidator::AssetBytes> ReadReplayFileUtf8(
        const std::string &path,
        const forevervalidator::ReplayIdentity &identity) noexcept {
    using forevervalidator::AssetBytes;
    using forevervalidator::Result;
    using forevervalidator::ValidationErrorCategory;
    using forevervalidator::ValidationErrorCode;
    using forevervalidator::ValidationFailureReason;

    try {
        if (path.empty()) {
            return Result<AssetBytes>::Failure(ReplayReadError(
                    ValidationErrorCategory::InvalidInput,
                    ValidationErrorCode::InvalidArgument,
                    ValidationFailureReason::EmptyReplayPath,
                    identity,
                    path,
                    "replay path is empty"));
        }

        const std::filesystem::path nativePath =
                std::filesystem::u8path(path);
        std::ifstream stream(
                nativePath, std::ios::binary | std::ios::ate);
        if (!stream) {
            return Result<AssetBytes>::Failure(ReplayReadError(
                    ValidationErrorCategory::Replay,
                    ValidationErrorCode::ReplayDecodingFailed,
                    ValidationFailureReason::ReplayFileOpenFailed,
                    identity,
                    path,
                    "could not open replay file"));
        }

        const std::streamoff length = stream.tellg();
        if (length <= 0 ||
            static_cast<std::uintmax_t>(length) >
                    std::numeric_limits<std::size_t>::max()) {
            return Result<AssetBytes>::Failure(ReplayReadError(
                    ValidationErrorCategory::Replay,
                    ValidationErrorCode::ReplayDecodingFailed,
                    ValidationFailureReason::ReplayFileLengthInvalid,
                    identity,
                    path,
                    "invalid replay file length"));
        }

        stream.seekg(0, std::ios::beg);
        AssetBytes bytes(static_cast<std::size_t>(length));
        if (!stream.read(
                    reinterpret_cast<char *>(bytes.data()), length)) {
            return Result<AssetBytes>::Failure(ReplayReadError(
                    ValidationErrorCategory::Replay,
                    ValidationErrorCode::ReplayDecodingFailed,
                    ValidationFailureReason::ReplayFileReadFailed,
                    identity,
                    path,
                    "could not read complete replay file"));
        }
        return Result<AssetBytes>::Success(std::move(bytes));
    } catch (const std::bad_alloc &) {
        return Result<AssetBytes>::Failure(ReplayReadError(
                ValidationErrorCategory::Allocation,
                ValidationErrorCode::AllocationFailed,
                ValidationFailureReason::AllocationFailed,
                identity,
                path,
                "allocation failed while reading replay"));
    } catch (...) {
        return Result<AssetBytes>::Failure(ReplayReadError(
                ValidationErrorCategory::Internal,
                ValidationErrorCode::UnexpectedFailure,
                ValidationFailureReason::UnexpectedFailure,
                identity,
                path,
                "unexpected failure while reading replay"));
    }
}

}  // namespace forevertas
