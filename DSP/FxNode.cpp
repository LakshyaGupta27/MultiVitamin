#include "FxNode.h"
#include "DelayFx.h"
#include "ReverbFx.h"
#include "FilterFx.h"
#include "GranularFx.h"
#include "DistortionFx.h"

std::unique_ptr<FxNode> FxNode::create(Type t) {
    switch (t) {
        case Type::Delay:       return std::make_unique<DelayFx>();
        case Type::Reverb:      return std::make_unique<ReverbFx>();
        case Type::Filter:      return std::make_unique<FilterFx>();
        case Type::Granular:    return std::make_unique<GranularFx>();
        case Type::Distortion:  return std::make_unique<DistortionFx>();
    }
    return std::make_unique<GranularFx>();
}
