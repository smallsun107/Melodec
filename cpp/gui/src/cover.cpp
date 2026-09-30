// rencm-gui - cover decoding for the player bar.
//
// The ONLY translation unit that compiles stb_image. Covers are usually JPEG,
// but the container does not promise that - NetEase also ships PNG covers - so
// both codecs are compiled in (a cover we cannot decode would just show the
// placeholder). Keeping only STBI_ONLY_JPEG here was a bug: PNG covers failed.

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "cover.hpp"

#include <filesystem>

#include <GLFW/glfw3.h>

namespace rencm::app {

namespace {

// decode_to_file() writes the cover next to the audio as <stem>.jpg.
std::string cover_path_for(const std::string& audio_path) {
    std::filesystem::path p(audio_path);
    p.replace_extension(".jpg");
    return p.string();
}

} // namespace

bool decode_cover(const std::string& path, CoverImage& out) {
    out = CoverImage{};
    if (path.empty()) return false;

    int w = 0, h = 0, channels = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &channels, 4); // force RGBA
    if (px == nullptr) return false;
    if (w <= 0 || h <= 0) {
        stbi_image_free(px);
        return false;
    }

    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + std::size_t(w) * std::size_t(h) * 4);
    stbi_image_free(px);
    return true;
}

Cover::~Cover() {
    if (tex_ != 0) glDeleteTextures(1, &tex_);
}

unsigned int Cover::get(const std::string& audio_path) {
    const std::string jpg = audio_path.empty() ? std::string{} : cover_path_for(audio_path);
    if (jpg == path_) return tex_; // already resolved, including "no cover"

    path_ = jpg;
    if (tex_ != 0) {
        glDeleteTextures(1, &tex_);
        tex_ = 0;
    }
    if (jpg.empty()) return 0;

    CoverImage img;
    if (!decode_cover(jpg, img)) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 img.rgba.data());

    tex_ = tex;
    return tex_;
}

} // namespace rencm::app
