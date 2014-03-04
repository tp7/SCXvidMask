#include <avisynth.h>
#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>

static std::string get_file_contents(const char* filename)
{
    std::ifstream in(filename, std::ios::in);
    if (!in)
    {
        return std::string();
    }
    std::string contents;

    in.seekg(0, std::ios::end);
    contents.resize((unsigned int)in.tellg());
    in.seekg(0, std::ios::beg);
    in.read(&contents[0], contents.size());
    in.close();
    contents.shrink_to_fit();
    return contents;
}

static std::vector<std::string> get_lines(const std::string &str)
{
    std::stringstream stream(str);
    std::vector<std::string> elements;
    std::string line;
    while (std::getline(stream, line))
    {
        elements.push_back(line);
    }
    return move(elements);
}

static bool starts_with(const std::string &str, const std::string &prefix)
{
    if (str.size() < prefix.size())
    {
        return false;
    }
    return str.substr(0, prefix.size()) == prefix;
}

class ScxMask : public GenericVideoFilter
{
    enum class FrameType
    {
        I,
        P,
        Weird
    };

    std::vector<FrameType> frame_types_;
    int offset_;
    bool strict_;

public:
    ScxMask(PClip child, const char *path, int offset, bool strict, IScriptEnvironment *env);

    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env) override; 

    int __stdcall SetCacheHints(int cachehints, int frame_range) override
    {
        return cachehints == CACHE_GET_MTMODE ? MT_NICE_FILTER : 0;
    };  
    
};

ScxMask::ScxMask(PClip child, char const *path, int offset, bool strict, IScriptEnvironment* env)
: GenericVideoFilter(child), offset_(offset), strict_(strict)
{
    if (path == nullptr)
    {
        env->ThrowError("CSXvidMask: no path passed");
    }
    auto content = get_file_contents(path);
    if (content.empty())
    {
        env->ThrowError("CSXvidMask: nonexistent or empty file passed");
    }
    auto lines = get_lines(content);
    if (!starts_with(lines[0], "# XviD 2pass stat file"))
    {
        env->ThrowError(":CSXvidMask file doesn't seem to be an XviD log");
    }
    frame_types_.reserve(lines.size() - 3);
    for (size_t i = 3; i < lines.size(); i++)
    {
        if (lines[i].empty())
        {
            continue;
        }
        auto frame_type = FrameType::Weird;
        switch (lines[i][0])
        {
        case 'p':
            frame_type = FrameType::P;
            break;
        case 'i':
            frame_type = FrameType::I;
            break;
        }
        frame_types_.push_back(frame_type);
    }
}

PVideoFrame ScxMask::GetFrame(int n, IScriptEnvironment* env)
{
    int frame = n + offset_;
    PVideoFrame dst = env->NewVideoFrame(vi);

    int value = 0;
    if (frame >= int(frame_types_.size()) || frame < 0)
    {
        if (strict_)
        {
            env->ThrowError("CSXvidMask: invalid frame requested. Maximum frame: %i. Set strict=false if you want to allow this.",
                frame_types_.size() - 1);
        }
    }
    else if (frame_types_[frame] == FrameType::I)
    {
        value = 255;
    }

    memset(dst->GetWritePtr(), value, dst->GetPitch() * dst->GetHeight());
    if (vi.IsPlanar() & !vi.IsY8())
    {
        memset(dst->GetWritePtr(PLANAR_U), value, dst->GetPitch(PLANAR_U) * dst->GetHeight(PLANAR_U));
        memset(dst->GetWritePtr(PLANAR_V), value, dst->GetPitch(PLANAR_V) * dst->GetHeight(PLANAR_V));
    }

    return dst;
}

AVSValue __cdecl create_csxmask(AVSValue args, void*, IScriptEnvironment* env)
{
    enum { CLIP, PATH, OFFSET, STRICT };

    return new ScxMask(args[CLIP].AsClip(), args[PATH].AsString(nullptr), -args[OFFSET].AsInt(0), args[STRICT].AsBool(false), env);
}

const AVS_Linkage *AVS_linkage = nullptr;

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("SCXvidMask", "c[path]s[offset]i[strict]b", create_csxmask, 0);
    return "Blame __ar";
}
