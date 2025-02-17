/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "onnx2trt.hpp"
#include "onnx2trt_utils.hpp"

#include <list>
#include <unordered_map>

namespace onnx2trt
{

class ImporterContext final : public IImporterContext
{
    nvinfer1::INetworkDefinition* _network;
    nvinfer1::ILogger* _logger;
    std::list<std::vector<uint8_t>> _temp_bufs;
    StringMap<nvinfer1::ITensor*> _user_inputs;
    StringMap<nvinfer1::ITensor**> _user_outputs;
    StringMap<int64_t> _opsets;
    StringMap<TensorOrWeights> mTensors; // All tensors in the graph mapped to their names.
    StringMap<nvinfer1::TensorLocation> mTensorLocations;
    StringMap<float> mTensorRangeMins;
    StringMap<float> mTensorRangeMaxes;
    StringMap<nvinfer1::DataType> mLayerPrecisions;
    std::set<std::string> mTensorNames; // Keep track of how many times a tensor name shows up, to avoid duplicate naming in TRT.
    std::set<std::string> mLayerNames; // Keep track of how many times a tensor name shows up, to avoid duplicate naming in TRT.
    int64_t mSuffixCounter = 0; // increasing suffix counter used to uniquify layer names.
    std::unordered_set<std::string> mUnsupportedShapeTensors; // Container to hold output tensor names of layers that produce shape tensor outputs but do not natively support them.
    StringMap<std::string> mLoopTensors; // Container to map subgraph tensors to their original outer graph names.
    std::string mOnnxFileLocation; // Keep track of the directory of the parsed ONNX file
    std::list<std::string> mInitializerNames; // Keep track of unique names of any initializers
    RefitMap_t* mRefitMap; // Keep track of names of ONNX refittable weights with their corresponding TRT layer and role

public:
    ImporterContext(nvinfer1::INetworkDefinition* network, nvinfer1::ILogger* logger, RefitMap_t* refitMap)
        : _network(network)
        , _logger(logger)
        , mRefitMap(refitMap)
    {
    }
    virtual nvinfer1::INetworkDefinition* network() override
    {
        return _network;
    }
    virtual StringMap<TensorOrWeights>& tensors() override
    {
        return mTensors;
    }
    virtual StringMap<nvinfer1::TensorLocation>& tensorLocations() override
    {
        return mTensorLocations;
    }
    virtual StringMap<float>& tensorRangeMins() override
    {
        return mTensorRangeMins;
    }
    virtual StringMap<float>& tensorRangeMaxes() override
    {
        return mTensorRangeMaxes;
    }
    virtual StringMap<nvinfer1::DataType>& layerPrecisions() override
    {
        return mLayerPrecisions;
    }
    virtual std::unordered_set<std::string>& unsupportedShapeTensors() override
    {
        return mUnsupportedShapeTensors;
    }
    virtual StringMap<std::string>& loopTensors() override
    {
        return mLoopTensors;
    }
    virtual void setOnnxFileLocation(std::string location) override
    {
        mOnnxFileLocation = location;
    }
    virtual std::string getOnnxFileLocation() override
    {
        return mOnnxFileLocation;
    }
    virtual void insertRefitMap(std::string weightsName, std::string layerName, nvinfer1::WeightsRole role) override
    {
        mRefitMap->insert({weightsName, WeightsPair_t{layerName, role}});
    }
    // This actually handles weights as well, but is named this way to be consistent with the tensors()
    virtual void registerTensor(TensorOrWeights tensor, const std::string& basename) override
    {
        // TRT requires unique tensor names.
        const std::string uniqueName = generateUniqueName(mTensorNames, basename);

        if (tensor)
        {
            auto* ctx = this; // To enable logging.
            if (tensor.is_tensor())
            {
                tensor.tensor().setName(uniqueName.c_str());

                LOG_VERBOSE("Registering tensor: " << uniqueName << " for ONNX tensor: " << basename);
            }
            else if (tensor.is_weights())
            {
                mInitializerNames.push_back(uniqueName);
                const auto& weights = tensor.weights();
                if (tensor.weights().type == ::ONNX_NAMESPACE::TensorProto::INT64)
                {
                    tensor = ShapedWeights{::ONNX_NAMESPACE::TensorProto::INT32,
                        convertINT64(reinterpret_cast<int64_t*>(weights.values), weights.shape, ctx), weights.shape};
                }
                tensor.weights().setName(mInitializerNames.back().c_str());
            }
        }
        // Overwrite previous tensors registered with the same name (this only happens when there are subgraphs,
        // and in that case, overwriting is the desired behavior).
        this->tensors()[basename] = std::move(tensor);
    }

    virtual void registerLayer(nvinfer1::ILayer* layer, const std::string& basename) override
    {
        // No layer will be added for Constant nodes in ONNX.
        if (layer)
        {
            const std::string name = basename.empty() ? layer->getName() : basename;
            const std::string uniqueName = generateUniqueName(mLayerNames, name);

            auto* ctx = this; // To enable logging.
            if (layer->getType() == nvinfer1::LayerType::kCONSTANT)
            {
                LOG_VERBOSE("Registering constant layer: " << uniqueName << " for ONNX initializer: " << basename);
            }
            else
            {
                LOG_VERBOSE("Registering layer: " << uniqueName << " for ONNX node: " << basename);
            }
            layer->setName(uniqueName.c_str());
        }
    }

    virtual nvinfer1::ILogger& logger() override
    {
        return *_logger;
    }

    virtual ShapedWeights createTempWeights(ShapedWeights::DataType type, nvinfer1::Dims shape) override
    {
        ShapedWeights weights(type, nullptr, shape);
        // Need special logic for handling scalars.
        if (shape.nbDims == 0)
        {
            _temp_bufs.push_back(std::vector<uint8_t>(getDtypeSize(type)));
        }
        else
        {
            _temp_bufs.push_back(std::vector<uint8_t>(weights.size_bytes()));
        }
        weights.values = _temp_bufs.back().data();
        return weights;
    }

    bool setUserInput(const char* name, nvinfer1::ITensor* input)
    {
        _user_inputs[name] = input;
        return true;
    }
    bool setUserOutput(const char* name, nvinfer1::ITensor** output)
    {
        _user_outputs[name] = output;
        return true;
    }
    nvinfer1::ITensor* getUserInput(const char* name)
    {
        if (!_user_inputs.count(name))
        {
            return nullptr;
        }
        else
        {
            return _user_inputs.at(name);
        }
    }
    nvinfer1::ITensor** getUserOutput(const char* name)
    {
        if (!_user_outputs.count(name))
        {
            return nullptr;
        }
        else
        {
            return _user_outputs.at(name);
        }
    }
    StringMap<nvinfer1::ITensor**> const& getUserOutputs() const
    {
        return _user_outputs;
    }
    void clearOpsets()
    {
        _opsets.clear();
    }
    void addOpset(std::string domain, int64_t version)
    {
        _opsets.emplace(domain, version);
    }
    virtual int64_t getOpsetVersion(const char* domain = "") const override
    {
        if (_opsets.empty())
        {
            return 1;
        }
        else if (_opsets.size() == 1)
        {
            return _opsets.begin()->second;
        }
        else
        {
            assert(_opsets.count(domain));
            return _opsets.at(domain);
        }
    }
private:
    std::string generateUniqueName(std::set<std::string>& namesSet, const std::string& basename)
    {
        std::string candidate = basename;

        while (namesSet.find(candidate) != namesSet.end())
        {
            candidate = basename + "_" + std::to_string(mSuffixCounter);
            ++mSuffixCounter;
        }

        namesSet.insert(candidate);

        return candidate;
    }
};

} // namespace onnx2trt
