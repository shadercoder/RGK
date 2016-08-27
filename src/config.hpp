#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__

#include <string>
#include <vector>
#include <exception>

#include "glm.hpp"
#include "brdf.hpp"
#include "primitives.hpp"
#include "camera.hpp"

#include "../external/json/json.h"

struct ConfigFileException : public std::runtime_error{
    ConfigFileException(const std::string& what ) : std::runtime_error(what) {}
};

class Config{
public:
    std::string config_file_path;

    std::string comment;
    std::string output_file;
    unsigned int recursion_level;
    unsigned int xres, yres;
    unsigned int multisample = 1;
    float bumpmap_scale = 10.0f;
    float clamp = 100000.0f;
    float russian = -1.0f;
    unsigned int rounds = 1;
    bool force_fresnell = false;
    unsigned int reverse = 0;
    std::string brdf = "cooktorr";
    std::vector<std::string> thinglass;

    virtual Camera GetCamera(float rotation) const = 0;
    virtual void InstallLights(Scene& scene) const = 0;
    virtual void  InstallScene(Scene& scene) const = 0;
    virtual std::pair<Color, float> GetSky() const = 0;
protected:
    Config(){};
};

class ConfigRTC : public Config{
public:
    static std::shared_ptr<ConfigRTC> CreateFromFile(std::string path);
    virtual Camera GetCamera(float rotation) const override;
    virtual void InstallLights(Scene& scene) const override;
    virtual void  InstallScene(Scene& scene) const override;
    virtual std::pair<Color, float> GetSky() const override;
private:
    ConfigRTC(){};

    // Pre-fetching all values when the file is loaded
    std::string model_file;
    glm::vec3 camera_position;
    glm::vec3 camera_lookat;
    glm::vec3 camera_upvector;
    float yview;
    std::vector<Light> lights;
    float lens_size = 0.0f;
    float focus_plane = 1.0f;
    Color sky_color = Color(0.0, 0.0, 0.0);
    float sky_brightness = 2.0;
};

class ConfigJSON : public Config{
public:
    static std::shared_ptr<ConfigJSON> CreateFromFile(std::string path);
    virtual Camera GetCamera(float rotation) const override;
    virtual void InstallLights(Scene& scene) const override;
    virtual void  InstallScene(Scene& scene) const override;
    virtual std::pair<Color, float> GetSky() const override;
private:
    ConfigJSON(){};

    Json::Value root;
};

#endif // __CONFIG_HPP__
