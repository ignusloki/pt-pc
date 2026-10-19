#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "engine/platform/graphics_presets.h"
int main(int argc,char** argv) {
    int failures=0;
    const auto check=[&](const char* name,bool ok){std::printf("%s: %s\n",name,ok?"PASS":"FAIL");failures+=!ok;};
    pt::AppSettings s;
    s.display.width=2560;s.display.height=1440;
    s.upscaling.upscaler="dlss";s.upscaling.scale=.8f;s.upscaling.quality="native";
    s.voice.device="test microphone";s.audio.volume=.6f;
    pt::ApplyGraphicsPreset(s,pt::GraphicsPreset::Low,true);
    check("Low reduces effects and shadows",s.graphics.shadow_quality==1 && !s.graphics.reflections && !s.graphics.ambient_occlusion && !s.graphics.bloom);
    check("Low retains output size and upscale choice",s.display.width==2560 && s.display.height==1440 && s.upscaling.scale==.8f && s.upscaling.upscaler=="dlss" && s.upscaling.quality=="native");
    check("preset retains sound and microphone",s.voice.device=="test microphone" && s.audio.volume==.6f);
    check("Low detected",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Low);
    pt::ApplyGraphicsPreset(s,pt::GraphicsPreset::Original,true);
    check("Original renderer defaults",s.graphics==pt::AppSettings{}.graphics && s.ray_tracing==pt::AppSettings{}.ray_tracing);
    check("Original detected",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Original);
    check("Original draws the flare ghosts",s.graphics.lens_ghosts);
    check("Original has no port dither (the PS4's output)",!s.graphics.deband);
    s.ray_tracing.contact_shadows=true;
    check("Original then contact shadows is Custom with the ghosts still on",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Custom && s.graphics.lens_ghosts);
    pt::ApplyGraphicsPreset(s,pt::GraphicsPreset::High,true);
    check("Low, High and Ultra keep the dither",[]{for(auto p:{pt::GraphicsPreset::Low,pt::GraphicsPreset::High,pt::GraphicsPreset::Ultra}){pt::AppSettings t;pt::ApplyGraphicsPreset(t,p,true);if(!t.graphics.deband) return false;}return true;}());
    check("High leaves the flare ghosts out",!s.graphics.lens_ghosts && pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::High);
    s.graphics.lens_ghosts=true;
    check("ghosts by hand make High Custom",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Custom);
    s.graphics.lens_ghosts=false;
    pt::ApplyGraphicsPreset(s,pt::GraphicsPreset::Ultra,true);
    check("Ultra has every traced effect and fine detail",s.ray_tracing.shadows==2 && s.ray_tracing.reflections && s.ray_tracing.ambient_occlusion && s.ray_tracing.contact_shadows && s.graphics.enhanced_textures==pt::kEnhancedTexturesSupported && s.graphics.ray_quality==4 && s.graphics.clarity==.4f);
    check("Ultra is path traced and keeps the Ultra tier parts set",s.ray_tracing.full_shadows && s.ray_tracing.bounce_light && s.ray_tracing.glossy_reflections);
    check("Ultra preserves the complete cinematic effects",s.graphics.depth_of_field && s.graphics.motion_blur && s.graphics.bloom && s.graphics.lens_distortion && s.graphics.film_grain==1.0f);
    check("Ultra detected",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Ultra);
    s.graphics.bloom=false;
    check("manual change becomes Custom",pt::DetectGraphicsPreset(s,true)==pt::GraphicsPreset::Custom);
    pt::ApplyGraphicsPreset(s,pt::GraphicsPreset::Ultra,false);
    check("unsupported RT retains available texture fallback",s.ray_tracing==pt::AppSettings{}.ray_tracing && s.graphics.enhanced_textures==pt::kEnhancedTexturesSupported && s.graphics.ambient_occlusion && s.graphics.reflections);
    check("unsupported Ultra keeps cinematic effects",s.graphics.depth_of_field && s.graphics.motion_blur && s.graphics.bloom && s.graphics.lens_distortion && s.graphics.film_grain==1.0f);
    check("unsupported Ultra detected",pt::DetectGraphicsPreset(s,false)==pt::GraphicsPreset::Ultra);
    // Android cannot retain enhanced textures or enable ray queries. Both preset directions must leave Custom.
    for (const auto preset : {pt::GraphicsPreset::Low, pt::GraphicsPreset::Original, pt::GraphicsPreset::High, pt::GraphicsPreset::Ultra}) {
        pt::AppSettings phone;
        phone.graphics.clarity = .9f;
        check("manual phone settings start Custom", pt::DetectGraphicsPreset(phone, false, false) == pt::GraphicsPreset::Custom);
        pt::ApplyGraphicsPreset(phone, preset, false, false);
        check("phone preset stays recognized after texture fallback", !phone.graphics.enhanced_textures &&
              phone.ray_tracing == pt::AppSettings::RayTracing{} && pt::DetectGraphicsPreset(phone, false, false) == preset);
        if (argc == 2) {
            pt::AppSettings restored;
            const auto path = std::filesystem::path(argv[1]);
            check("phone preset persists with upstream settings", pt::SaveAppSettings(path, phone) &&
                  pt::LoadAppSettings(path, restored) && pt::DetectGraphicsPreset(restored, false, false) == preset);
        }
        pt::ApplyGraphicsPreset(phone, pt::GraphicsPreset::Low, false, false);
        check("phone preset can return to Low", pt::DetectGraphicsPreset(phone, false, false) == pt::GraphicsPreset::Low);
    }
    check("unsupported Ultra keeps the Ultra ray quality",s.graphics.ray_quality==3);
    {
        pt::AppSettings traced;pt::ApplyGraphicsPreset(traced,pt::GraphicsPreset::Ultra,true);
        check("Ultra picks Path traced",traced.graphics.ray_quality==4);
        traced.graphics.ray_quality=3;
        check("Ultra with the Ultra ray quality reads as Custom",pt::DetectGraphicsPreset(traced,true)==pt::GraphicsPreset::Custom);
    }
    if(argc==2) {
        const auto path=std::filesystem::path(argv[1]);
        check("save graphics settings",pt::SaveAppSettings(path,s));
        pt::AppSettings restored;
        check("load graphics settings",pt::LoadAppSettings(path,restored));
        check("graphics persistence",restored.graphics==s.graphics && restored.ray_tracing==s.ray_tracing);
        const auto without_key=[&](pt::GraphicsPreset preset){
            pt::AppSettings written;pt::ApplyGraphicsPreset(written,preset,true);
            if(!pt::SaveAppSettings(path,written)) return false;
            std::string text;{std::ifstream in(path);std::stringstream ss;ss<<in.rdbuf();text=ss.str();}
            const auto at=text.find("lens_ghosts = ");if(at==std::string::npos) return false;
            text.erase(at,text.find('\n',at)+1-at);
            {std::ofstream out(path);out<<text;}
            pt::AppSettings loaded;return pt::LoadAppSettings(path,loaded) && loaded.graphics.lens_ghosts==(preset==pt::GraphicsPreset::Original) &&
                   pt::DetectGraphicsPreset(loaded,true)==preset;
        };
        check("missing lens_ghosts keeps High as High without ghosts",without_key(pt::GraphicsPreset::High));
        check("missing lens_ghosts keeps Original with ghosts",without_key(pt::GraphicsPreset::Original));
        check("missing lens_ghosts keeps Ultra as Ultra",without_key(pt::GraphicsPreset::Ultra));
        const auto without_deband=[&](pt::GraphicsPreset preset,bool rt,bool custom,bool drop_ghosts){
            pt::AppSettings written;pt::ApplyGraphicsPreset(written,preset,rt);
            if(custom) written.graphics.bloom=!written.graphics.bloom;
            if(!pt::SaveAppSettings(path,written)) return false;
            std::string text;{std::ifstream in(path);std::stringstream ss;ss<<in.rdbuf();text=ss.str();}
            for(const char* key:{"deband = ","lens_ghosts = "}) {
                if(!drop_ghosts && key[0]=='l') continue;
                const auto at=text.find(key);if(at==std::string::npos) return false;
                text.erase(at,text.find('\n',at)+1-at);
            }
            {std::ofstream out(path);out<<text;}
            pt::AppSettings loaded;if(!pt::LoadAppSettings(path,loaded)) return false;
            return loaded.graphics.deband==(preset!=pt::GraphicsPreset::Original || custom);
        };
        check("missing deband keeps Original without the dither (rt)",without_deband(pt::GraphicsPreset::Original,true,false,false));
        check("missing deband keeps Original without the dither (no rt)",without_deband(pt::GraphicsPreset::Original,false,false,false));
        check("missing deband and lens_ghosts keeps Original without the dither",without_deband(pt::GraphicsPreset::Original,true,false,true));
        check("missing deband keeps Low with the dither",without_deband(pt::GraphicsPreset::Low,true,false,false));
        check("missing deband keeps High with the dither",without_deband(pt::GraphicsPreset::High,true,false,false));
        check("missing deband keeps Ultra with the dither",without_deband(pt::GraphicsPreset::Ultra,true,false,false));
        check("missing deband keeps a custom set with the dither",without_deband(pt::GraphicsPreset::Original,true,true,false));
        const auto before_full=[&](bool rt,bool custom){
            pt::AppSettings written;pt::ApplyGraphicsPreset(written,pt::GraphicsPreset::Ultra,rt);
            if(custom) written.graphics.bloom=false;
            if(!pt::SaveAppSettings(path,written)) return -1;
            std::string text;{std::ifstream in(path);std::stringstream ss;ss<<in.rdbuf();text=ss.str();}
            {const auto at=text.find("\nray_quality = ");if(at==std::string::npos) return -1;text.replace(at+1,text.find('\n',at+1)-at-1,"ray_quality = 2");}
            for(const char* key:{"full_shadows = ","bounce_light = ","glossy_reflections = "}) {
                const auto at=text.find(key);if(at==std::string::npos) return -1;
                text.erase(at,text.find('\n',at)+1-at);
            }
            {std::ofstream out(path);out<<text;}
            pt::AppSettings loaded;if(!pt::LoadAppSettings(path,loaded)) return -1;
            return loaded.graphics.ray_quality*10+(pt::DetectGraphicsPreset(loaded,rt)==pt::GraphicsPreset::Ultra?1:0);
        };
        check("an old Ultra pt.ini keeps Ultra rays instead of selecting Path traced",before_full(true,false)==30);
        check("a custom pt.ini from before Full reads as the new Ultra",before_full(true,true)==30);
        check("an Ultra pt.ini without ray tracing from before Full stays Ultra",before_full(false,false)==31);
    }
    return failures?1:0;
}
