#pragma once

#include <QString>
#include <stdexcept>

namespace buildscope::native {

// Error family mirroring the Python producer's exception taxonomy. Every
// failure mode carries a human-readable message and aborts the operation.
class NativeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    explicit NativeError(const QString &message) : std::runtime_error(message.toStdString()) {}
};

class CommandError final : public NativeError {
public:
    using NativeError::NativeError;
};

class PathNormalizationError final : public NativeError {
public:
    using NativeError::NativeError;
};

class NormalizationError final : public NativeError {
public:
    using NativeError::NativeError;
};

class SnapshotError final : public NativeError {
public:
    using NativeError::NativeError;
};

class SnapshotIoError final : public NativeError {
public:
    using NativeError::NativeError;
};

class DiffError final : public NativeError {
public:
    using NativeError::NativeError;
};

class DiffPolicyError final : public NativeError {
public:
    using NativeError::NativeError;
};

class IncludeAnalysisError final : public NativeError {
public:
    using NativeError::NativeError;
};

}  // namespace buildscope::native
