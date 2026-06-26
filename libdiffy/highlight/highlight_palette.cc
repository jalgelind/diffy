#include "highlight/highlight_palette.hpp"

namespace diffy {

HlRgb
syntax_color(HighlightGroup group, bool light) {
    if (light) {
        switch (group) {
            case HighlightGroup::Comment:         return {0x00, 0x80, 0x00};
            case HighlightGroup::Keyword:         return {0x00, 0x00, 0xff};
            case HighlightGroup::Operator:        return {0x00, 0x00, 0x00};
            case HighlightGroup::Punctuation:     return {0x39, 0x39, 0x39};
            case HighlightGroup::String:          return {0xa3, 0x15, 0x15};
            case HighlightGroup::Escape:          return {0xee, 0x00, 0x00};
            case HighlightGroup::Number:          return {0x09, 0x86, 0x58};
            case HighlightGroup::Boolean:         return {0x00, 0x00, 0xff};
            case HighlightGroup::Constant:        return {0x00, 0x70, 0xc1};
            case HighlightGroup::ConstantBuiltin: return {0x00, 0x00, 0xff};
            case HighlightGroup::Function:        return {0x79, 0x5e, 0x26};
            case HighlightGroup::Method:          return {0x79, 0x5e, 0x26};
            case HighlightGroup::Constructor:     return {0x26, 0x7f, 0x99};
            case HighlightGroup::Type:            return {0x26, 0x7f, 0x99};
            case HighlightGroup::TypeBuiltin:     return {0x00, 0x00, 0xff};
            case HighlightGroup::Variable:        return {0x00, 0x10, 0x80};
            case HighlightGroup::Parameter:       return {0x00, 0x10, 0x80};
            case HighlightGroup::Property:        return {0x00, 0x10, 0x80};
            case HighlightGroup::Namespace:       return {0x26, 0x7f, 0x99};
            case HighlightGroup::Label:           return {0x00, 0x00, 0x00};
            case HighlightGroup::Tag:             return {0x80, 0x00, 0x00};
            case HighlightGroup::Attribute:       return {0xe5, 0x00, 0x00};
            case HighlightGroup::None:
            default:                              return {0x24, 0x29, 0x2e};
        }
    }
    switch (group) {
        case HighlightGroup::Comment:         return {0x6a, 0x99, 0x55};
        case HighlightGroup::Keyword:         return {0x56, 0x9c, 0xd6};
        case HighlightGroup::Operator:        return {0xd4, 0xd4, 0xd4};
        case HighlightGroup::Punctuation:     return {0xcc, 0xcc, 0xcc};
        case HighlightGroup::String:          return {0xce, 0x91, 0x78};
        case HighlightGroup::Escape:          return {0xd7, 0xba, 0x7d};
        case HighlightGroup::Number:          return {0xb5, 0xce, 0xa8};
        case HighlightGroup::Boolean:         return {0x56, 0x9c, 0xd6};
        case HighlightGroup::Constant:        return {0x4f, 0xc1, 0xff};
        case HighlightGroup::ConstantBuiltin: return {0x56, 0x9c, 0xd6};
        case HighlightGroup::Function:        return {0xdc, 0xdc, 0xaa};
        case HighlightGroup::Method:          return {0xdc, 0xdc, 0xaa};
        case HighlightGroup::Constructor:     return {0x4e, 0xc9, 0xb0};
        case HighlightGroup::Type:            return {0x4e, 0xc9, 0xb0};
        case HighlightGroup::TypeBuiltin:     return {0x56, 0x9c, 0xd6};
        case HighlightGroup::Variable:        return {0x9c, 0xdc, 0xfe};
        case HighlightGroup::Parameter:       return {0x9c, 0xdc, 0xfe};
        case HighlightGroup::Property:        return {0x9c, 0xdc, 0xfe};
        case HighlightGroup::Namespace:       return {0x4e, 0xc9, 0xb0};
        case HighlightGroup::Label:           return {0xc8, 0xc8, 0xc8};
        case HighlightGroup::Tag:             return {0x56, 0x9c, 0xd6};
        case HighlightGroup::Attribute:       return {0x9c, 0xdc, 0xfe};
        case HighlightGroup::None:
        default:                              return {0xd4, 0xd4, 0xd4};
    }
}

}  // namespace diffy
