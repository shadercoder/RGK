#include "path_tracer.hpp"

#include "camera.hpp"
#include "scene.hpp"

#include <glm/gtx/vector_angle.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/constants.hpp>

Radiance PathTracer::RenderPixel(int x, int y, unsigned int & raycount, bool debug){
    Radiance total;

    if(debug) std::cout << std::endl;

    // N - rooks
    std::vector<unsigned int> V(multisample);
    for(unsigned int i = 0; i < V.size(); i++) V[i] = i;
    std::random_shuffle(V.begin(), V.end());

    for(unsigned int i = 0; i < V.size(); i++){
        Ray r;
        if(camera.IsSimple()){
            r = camera.GetSubpixelRay(x, y, xres, yres, i, V[i], multisample);
        }else{
            r = camera.GetSubpixelRayLens(x, y, xres, yres, i, V[i], multisample, rnd);
        }
        total += TracePath(r, raycount, debug);
    }

    if(debug) std::cout << "-----> pixel average: " << total/multisample << std::endl;

    return total / multisample;
}

float Fresnel(glm::vec3 I, glm::vec3 N, float ior){
    float cosi = glm::dot(I, N);
    float etai = 1.0f, etat = ior;
    if (cosi > 0) { std::swap(etai, etat); }
    // Compute sini using Snell's law
    float sint = etai / etat * glm::sqrt(glm::max(0.f, 1.0f - cosi * cosi));
    if(sint >= 1){
        // Total internal reflection
        return 1.0f;
    }else{
        float cost = sqrtf(std::max(0.f, 1 - sint * sint));
        cosi = fabsf(cosi);
        float Rs = ((etat * cosi) - (etai * cost)) / ((etat * cosi) + (etai * cost));
        float Rp = ((etai * cosi) - (etat * cost)) / ((etai * cosi) + (etat * cost));
        return (Rs * Rs + Rp * Rp) / 2.0f;
    }
}

Radiance PathTracer::TracePath(const Ray& r, unsigned int& raycount, bool debug){

    // First, generate a path.
    struct PathPoint{
        enum Type{
            SCATTERED,
            REFLECTED,
            ENTERED,
            LEFT,
        };
        Type type;
        bool infinity = false;
        Intersection i;
        glm::vec3 pos;
        glm::vec3 lightN;
        glm::vec3 faceN;
        glm::vec3 Vr; // outgoing direction (pointing towards previous path point)
        glm::vec3 Vi; // incoming direction (pointing towards next path point)
        glm::vec2 texUV;
        Radiance to_prev;
    };
    std::vector<PathPoint> path;

    Ray current_ray = r;
    unsigned int n = 0, n2 = 0;
    // Temporarily setting this to true ensures that russian roulette will not terminate (once).
    bool skip_russian = false;
    // Used for tracking index of refraction
    // float current_ior = 1.0f;
    const Triangle* last_triangle = nullptr;
    while(true){
        n++; n2++;
        if(n2 >= 20) break; // hard limit
        if(russian >= 0.0f){
            // Russian roulette path termination
            if(n > 1 && !skip_russian && rnd.Get01() > russian) break;
            skip_russian = false;
        }else{
            // Fixed depth path termination
            if(n > depth) break;
        }

        if(debug) std::cout << "Generating path, n = " << n << std::endl;

        raycount++;
        Intersection i = scene.FindIntersectKdOtherThan(current_ray, /* last_triangle */ nullptr, debug);
        PathPoint p;
        p.i = i;
        if(!i.triangle){
            p.infinity = true;
            p.Vr = -current_ray.direction;
            path.push_back(p);
            break;
        }else{
            if(i.triangle == last_triangle){
                // std::cerr << "Ray collided with source triangle. This should never happen." << std::endl;
            }
            // Prepare normal
            p.pos = current_ray[i.t];
            p.faceN = i.Interpolate(i.triangle->GetNormalA(),
                                    i.triangle->GetNormalB(),
                                    i.triangle->GetNormalC());
            // Prepare incoming direction
            p.Vr = -current_ray.direction;

            bool fromInside = false;
            if(glm::dot(p.faceN, p.Vr) < 0){
                fromInside = true;
            }

            // Interpolate textures
            const Material& mat = i.triangle->GetMaterial();
            if(mat.ambient_texture || mat.diffuse_texture || mat.specular_texture || mat.bump_texture){
                p.texUV = i.Interpolate(i.triangle->GetTexCoordsA(),
                                        i.triangle->GetTexCoordsB(),
                                        i.triangle->GetTexCoordsC());
                if(debug) std::cout << "diff texture " << mat.diffuse_texture << std::endl;
                if(debug) std::cout << "texUV " << p.texUV << std::endl;
            }
            // Tilt normal using bump texture
            if(mat.bump_texture){
                float right = mat.bump_texture->GetSlopeRight(p.texUV);
                float bottom = mat.bump_texture->GetSlopeBottom(p.texUV);
                glm::vec3 tangent = i.Interpolate(i.triangle->GetTangentA(),
                                                  i.triangle->GetTangentB(),
                                                  i.triangle->GetTangentC());
                glm::vec3 bitangent = glm::normalize(glm::cross(p.faceN,tangent));
                p.lightN = glm::normalize(p.faceN + (tangent*right + bitangent*bottom) * bumpmap_scale);
                if(debug) std::cout << "lightN " << p.lightN << std::endl;
            }else{
                p.lightN = p.faceN;
            }


            // Determine point type
            if(mat.translucency > 0.001f){
                // This is a translucent material.
                if(fromInside){
                    // Ray leaves the object
                    p.type = PathPoint::LEFT;
                    // Do not count this point into depth. Never russian-terminate path at this point.
                    n--; skip_russian = true;
                }else{
                    if(rnd.Get01() < mat.translucency){
                        // Fresnell refraction/reflection
                        // TODO: The fresnell function assumes eta1 = 1.0. For eg. underwater reflections this is
                        // not correct, really.
                        float q = Fresnel(p.Vr, p.lightN, 1.0/mat.refraction_index);
                        if(debug) std::cout << "Angle = " << glm::angle(p.Vr, p.lightN)*180.0f/glm::pi<float>() << std::endl;
                        if(debug) std::cout << "Fresnel = " << q << std::endl;
                        if(rnd.Get01() < q){
                            p.type = PathPoint::REFLECTED;
                        }else{
                            p.type = PathPoint::ENTERED;
                        }
                        // Do not count this point into depth. Never russian-terminate path at this point.
                        n--; skip_russian = true;
                    }else{
                        p.type = PathPoint::SCATTERED;
                    }
                }
            }else{
                // Not a translucent material.
                if(mat.reflective){
                    if(rnd.Get01() < mat.reflection_strength){
                        p.type = PathPoint::REFLECTED;
                        // Do not count this point into depth. Never russian-terminate path at this point.
                        n--; skip_russian = true;
                    }else{
                        p.type = PathPoint::SCATTERED;
                    }
                }else{
                    p.type = PathPoint::SCATTERED;
                }
            }

            if(debug) std::cout << "Ray hit material " << mat.name << " and ";
            // Generate next ray direction
            glm::vec3 dir;
            switch(p.type){
            case PathPoint::SCATTERED:
                if(debug) std::cout << "SCATTERED." << std::endl;
                dir = rnd.GetHSCosDir(p.faceN);
                while(glm::angle(dir, p.lightN) > glm::pi<float>()/2.0f)
                    dir = rnd.GetHSCosDir(p.faceN);
                break;
            case PathPoint::REFLECTED:
                if(debug) std::cout << "REFLECTED." << std::endl;
                dir = 2.0f * glm::dot(p.Vr, p.lightN) * p.lightN - p.Vr;
                break;
            case PathPoint::ENTERED:
                // TODO: Refraction
                dir = glm::refract(p.Vr, p.lightN, 1.0f/mat.refraction_index);
                if(debug) std::cout << "ENTERED medium." << std::endl;
                if(glm::length(dir) < 0.001f || glm::isnan(dir.x)){
                    // Internal reflection
                    if(debug) std::cout << "internally reflected." << std::endl;
                    p.type = PathPoint::REFLECTED;
                    dir = 2.0f * glm::dot(p.Vr, p.lightN) * p.lightN - p.Vr;
                }
                // current_ior *= mat.refraction_index
                break;
            case PathPoint::LEFT:
                // TODO: Refraction
                if(debug) std::cout << "LEFT medium." << std::endl;
                dir = glm::refract(p.Vr, p.lightN, mat.refraction_index);
                if(glm::length(dir) < 0.001f){
                    // Internal reflection
                    if(debug) std::cout << "internally reflected." << std::endl;
                    p.type = PathPoint::REFLECTED;
                    dir = 2.0f * glm::dot(p.Vr, p.lightN) * p.lightN - p.Vr;
                }
                dir = -p.Vr;
                // current_ior /= mat.refraction_index;
                break;
            }
            p.Vi = dir;

            path.push_back(p);

            current_ray = Ray(p.pos +
                              p.faceN * scene.epsilon * 10.0f *
                              ((p.type == PathPoint::ENTERED)?-1.0f:1.0f)
                              , glm::normalize(dir));

            if(debug) std::cout << "Next ray will be from " << p. pos << " dir " << dir << std::endl;

            last_triangle = i.triangle;
            // Continue for next ray
        }
    }


    for(int n = path.size()-1; n >= 0; n--){
        if(debug) std::cout << "--- Processing PP " << n << std::endl;

        bool last = ((unsigned int)n == path.size()-1);
        PathPoint& pp = path[n];
        if(pp.infinity){
            if(debug) std::cout << "This a sky ray, total: " << sky_radiance << std::endl;
            pp.to_prev = sky_radiance;
        }else{
            const Material& mat = pp.i.triangle->GetMaterial();

            if(debug) std::cout << "Hit material: " << mat.name << std::endl;

            if(debug) std::cout << "texUV " << pp.texUV << std::endl;

            Color diffuse  =  mat.diffuse_texture?mat.diffuse_texture->GetPixelInterpolated(pp.texUV,debug) : mat.diffuse ;
            Color specular = mat.specular_texture?mat.specular_texture->GetPixelInterpolated(pp.texUV,debug): mat.specular;

            Radiance total;

            if(pp.type == PathPoint::SCATTERED){
                // Direct lighting, random light
                int light_n = rnd.GetInt(0, lights.size() -1 );
                const Light& l = lights[light_n];
                glm::vec3 lightpos = l.pos + rnd.GetSphere(l.size);

                if(debug) std::cout << "Incorporating direct lighting component, lightpos: " << lightpos << std::endl;

                // Visibility factor
                if(scene.Visibility(lightpos, pp.pos)){

                    if(debug) std::cout << "Light is visible" << std::endl;

                    // Incoming direction
                    glm::vec3 Vi = glm::normalize(lightpos - pp.pos);

                    Radiance f = mat.brdf(pp.lightN, diffuse, specular, Vi, pp.Vr, mat.exponent, 1.0, mat.refraction_index);

                    if(debug) std::cout << "f = " << f << std::endl;

                    float G = glm::max(0.0f, glm::cos( glm::angle(pp.lightN, Vi) )) / glm::distance2(lightpos, pp.pos);
                    if(debug) std::cout << "G = " << G << ", angle " << glm::angle(pp.lightN, Vi) << std::endl;
                    if(debug) std::cout << "lightN = " << pp.lightN << ", Vi " << Vi << std::endl;

                    Radiance inc_l = Radiance(l.color) * l.intensity;

                    Radiance out = inc_l * f * G;
                    if(debug) std::cout << "total direct lighting: " << out << std::endl;
                    total += out;
                }

                // indirect lighting
                if(!last){
                    // look at next pp's to_prev and incorporate it here
                    Radiance incoming = path[n+1].to_prev;
                    if(debug) std::cout << "Incorporating indirect lighting - incoming radiance: " << incoming << std::endl;

                    if(russian > 0.0f) incoming = incoming / russian;

                    if(debug) std::cout << "With russian: " << incoming << std::endl;

                    // Incoming direction
                    glm::vec3 Vi = pp.Vi;

                    if(debug) std::cout << "Indirect incoming from: " << Vi << std::endl;

                    Radiance f = mat.brdf(pp.lightN, diffuse, specular, Vi, pp.Vr, mat.exponent, 1.0, mat.refraction_index);

                    if(debug) std::cout << "BRDF: " << f << std::endl;

                    Radiance inc = incoming * f * glm::pi<float>(); // * glm::dot(pp.lightN, Vi);

                    if(debug) std::cout << "Incoming * brdf * pi = " << inc << std::endl;


                    total += inc;
                }
            }else if(pp.type == PathPoint::REFLECTED){
                Radiance incoming = path[n+1].to_prev;
                total += incoming;
            }else if(pp.type == PathPoint::ENTERED){
                Radiance incoming = path[n+1].to_prev;
                total += incoming * diffuse;
            }else if(pp.type == PathPoint::LEFT){
                Radiance incoming = path[n+1].to_prev;
                total += incoming;
            }

            if(debug) std::cerr << "total: " << total << std::endl;

            if(total.r > clamp) total.r = clamp;
            if(total.g > clamp) total.g = clamp;
            if(total.b > clamp) total.b = clamp;

            // Whoops.
            if(glm::isnan(total.r) || total.r < 0.0f) total.r = 0.0f;
            if(glm::isnan(total.g) || total.g < 0.0f) total.g = 0.0f;
            if(glm::isnan(total.b) || total.b < 0.0f) total.b = 0.0f;


            pp.to_prev = total;
        }
    }
    if(debug) std::cerr << "PATH TOTAL" << path[0].to_prev << std::endl << std::endl;
    return path[0].to_prev;
}
