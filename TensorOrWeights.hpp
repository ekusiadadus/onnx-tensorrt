/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "ShapedWeights.hpp"

#include <NvInfer.h>
#include <cassert>

namespace onnx2trt
{

class TensorOrWeights
{
    union
    {
        nvinfer1::ITensor* _tensor;
        ShapedWeights _weights;
    };
    enum
    {
        NODE_TENSOR,
        NODE_WEIGHTS
    } _variant;

public:
    TensorOrWeights()
        : _tensor(nullptr)
        , _variant(NODE_TENSOR)
    {
    }
    TensorOrWeights(nvinfer1::ITensor* tensor)
        : _tensor(tensor)
        , _variant(NODE_TENSOR)
    {
    }
    TensorOrWeights(ShapedWeights const& weights)
        : _weights(weights)
        , _variant(NODE_WEIGHTS)
    {
    }
    bool is_tensor() const
    {
        return _variant == NODE_TENSOR;
    }
    bool is_weights() const
    {
        return _variant == NODE_WEIGHTS;
    }
    bool isNullTensor() const
    {
        return is_tensor() && _tensor == nullptr;
    }
    nvinfer1::ITensor& tensor()
    {
        assert(!isNullTensor());
        return *_tensor;
    }
    nvinfer1::ITensor const& tensor() const
    {
        assert(!isNullTensor());
        return *_tensor;
    }
    ShapedWeights& weights()
    {
        assert(is_weights());
        return _weights;
    }
    ShapedWeights const& weights() const
    {
        assert(is_weights());
        return _weights;
    }
    nvinfer1::Dims shape() const
    {
        return is_tensor() ? _tensor->getDimensions() : _weights.shape;
    }
    explicit operator bool() const
    {
        return is_tensor() ? _tensor != nullptr : static_cast<bool>(_weights);
    }
    bool isInt32() const
    {
        return is_tensor() ? _tensor->getType() == nvinfer1::DataType::kINT32 : _weights.type == ::ONNX_NAMESPACE::TensorProto_DataType_INT32;
    }
    bool isBool() const
    {
        return is_tensor() ? _tensor->getType() == nvinfer1::DataType::kBOOL : _weights.type == ::ONNX_NAMESPACE::TensorProto_DataType_BOOL;
    }
    std::string getName() const
    {
        return is_tensor() ? _tensor->getName() : _weights.getName();
    }
};

} // namespace onnx2trt
