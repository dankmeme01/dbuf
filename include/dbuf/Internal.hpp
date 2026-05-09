#pragma once

#include <string>
#include <span>
#include <stdint.h>
#include <stddef.h>
#include "Result.hpp"

namespace dbuf {

using Error = std::string;
template <typename T = void>
using Result = geode::Result<T, Error>;

using geode::Ok;
using geode::Err;

// IO read traits
template <typename T>
concept ReadSource = requires(T reader, uint8_t* data, size_t size) {
    { reader.read(data, size) } -> std::same_as<Result<void>>;
};

template <typename T>
concept SeekSource = ReadSource<T> && requires(T reader, size_t pos) {
    { reader.position() } -> std::same_as<size_t>;
    { reader.setPosition(pos) } -> std::same_as<Result<void>>;
    { reader.totalSize() } -> std::same_as<size_t>;
};

template <typename T>
concept SkipSource = ReadSource<T> && requires(T reader, size_t size) {
    { reader.skip(size) } -> std::same_as<Result<void>>;
};

// IO write traits
template <typename T>
concept AnyWriteSink = requires(T writer, const uint8_t* data, size_t size) {
    { writer.write(data, size) };
};

template <typename T>
concept TryWriteSink = AnyWriteSink<T> && requires(T writer, const uint8_t* data, size_t size) {
    { writer.write(data, size) } -> std::same_as<Result<void>>;
};

template <typename T>
concept WriteSink = AnyWriteSink<T> && requires(T writer, const uint8_t* data, size_t size) {
    { writer.write(data, size) } -> std::same_as<void>;
};

template <typename T>
concept WriteZeroesSink = AnyWriteSink<T> && requires(T writer, size_t size) {
    { writer.writeZeroes(size) };
};

template <typename T>
concept SeekWriteSink = AnyWriteSink<T> && requires(T writer, size_t pos, size_t size) {
    { writer.setPosition(pos) } -> std::same_as<Result<void>>;
    { writer.position() } -> std::same_as<size_t>;
    { writer.slice(pos, size) } -> std::same_as<std::span<const uint8_t>>;
    { writer.written() } -> std::same_as<std::span<const uint8_t>>;
};

template <typename T>
concept IntoInnerSink = requires(T&& writer) {
    { std::move(writer).intoInner() };
};

}
