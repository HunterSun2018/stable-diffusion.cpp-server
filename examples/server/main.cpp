#ifndef NDEBUG
// crash the server in debug mode, otherwise send an http 500 error
#define CPPHTTPLIB_NO_EXCEPTIONS 1
#endif
// increase max payload length to allow use of larger context size
#define CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH 1048576

#include <signal.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>

#include "httplib.h"
#include "json.hpp"
#include "server.hpp"
#include "utils.hpp"

using json = nlohmann::json;

#include "stable-diffusion.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

const char* rng_type_to_str[] = {
    "std_default",
    "cuda",
};

// Names of the sampler method, same order as enum sample_method in stable-diffusion.h
const char* sample_method_str[] = {
    "euler_a",
    "euler",
    "heun",
    "dpm2",
    "dpm++2s_a",
    "dpm++2m",
    "dpm++2mv2",
    "lcm",
};

// Names of the sigma schedule overrides, same order as sample_schedule in stable-diffusion.h
const char* schedule_str[] = {
    "default",
    "discrete",
    "karras",
};

const char* modes_str[] = {
    "txt2img",
    "img2img",
    "img2vid",
    "convert",
};

enum SDMode {
    TXT2IMG,
    IMG2IMG,
    IMG2VID,
    CONVERT,
    MODE_COUNT
};
struct SDParams {
    int n_threads = -1;
    SDMode mode   = TXT2IMG;

    std::string model_path;
    std::string vae_path;
    std::string taesd_path;
    std::string esrgan_path;
    std::string controlnet_path;
    std::string embeddings_path;
    std::string stacked_id_embeddings_path;
    std::string input_id_images_path;
    sd_type_t wtype = SD_TYPE_COUNT;
    std::string lora_model_dir;
    std::string output_path = "shared/";
    std::string input_path;
    std::string control_image_path;

    std::string prompt;
    std::string negative_prompt;
    float min_cfg     = 1.0f;
    float cfg_scale   = 7.0f;
    float style_ratio = 20.f;
    int clip_skip     = -1;  // <= 0 represents unspecified
    int width         = 1024;
    int height        = 1024;
    int batch_count   = 1;

    int video_frames         = 6;
    int motion_bucket_id     = 127;
    int fps                  = 6;
    float augmentation_level = 0.f;

    sample_method_t sample_method = EULER_A;
    schedule_t schedule           = DEFAULT;
    int sample_steps              = 20;
    float strength                = 0.75f;
    float control_strength        = 0.9f;
    rng_type_t rng_type           = CUDA_RNG;
    int64_t seed                  = 42;
    bool verbose                  = false;
    bool vae_tiling               = false;
    bool control_net_cpu          = false;
    bool normalize_input          = false;
    bool clip_on_cpu              = false;
    bool vae_on_cpu               = false;
    bool canny_preprocess         = false;
    bool color                    = false;
    int upscale_repeats           = 1;

    std::string host = "0.0.0.0";
    uint16_t port    = 8080;

    size_t hash() const {
        return Hash::hash(this->prompt, this->negative_prompt, this->seed);
    }
};

void print_params(SDParams params) {
    printf("Option: \n");
    printf("    n_threads:         %d\n", params.n_threads);
    printf("    mode:              %s\n", modes_str[params.mode]);
    printf("    model_path:        %s\n", params.model_path.c_str());
    printf("    wtype:             %s\n", params.wtype < SD_TYPE_COUNT ? sd_type_name(params.wtype) : "unspecified");
    printf("    vae_path:          %s\n", params.vae_path.c_str());
    printf("    taesd_path:        %s\n", params.taesd_path.c_str());
    printf("    esrgan_path:       %s\n", params.esrgan_path.c_str());
    printf("    controlnet_path:   %s\n", params.controlnet_path.c_str());
    printf("    embeddings_path:   %s\n", params.embeddings_path.c_str());
    printf("    output_path:       %s\n", params.output_path.c_str());
    printf("    init_img:          %s\n", params.input_path.c_str());
    printf("    control_image:     %s\n", params.control_image_path.c_str());
    printf("    controlnet cpu:    %s\n", params.control_net_cpu ? "true" : "false");
    printf("    strength(control): %.2f\n", params.control_strength);
    printf("    prompt:            %s\n", params.prompt.c_str());
    printf("    negative_prompt:   %s\n", params.negative_prompt.c_str());
    printf("    min_cfg:           %.2f\n", params.min_cfg);
    printf("    cfg_scale:         %.2f\n", params.cfg_scale);
    printf("    clip_skip:         %d\n", params.clip_skip);
    printf("    width:             %d\n", params.width);
    printf("    height:            %d\n", params.height);
    printf("    sample_method:     %s\n", sample_method_str[params.sample_method]);
    printf("    schedule:          %s\n", schedule_str[params.schedule]);
    printf("    sample_steps:      %d\n", params.sample_steps);
    printf("    strength(img2img): %.2f\n", params.strength);
    printf("    rng:               %s\n", rng_type_to_str[params.rng_type]);
    printf("    seed:              %ld\n", params.seed);
    printf("    batch_count:       %d\n", params.batch_count);
    printf("    vae_tiling:        %s\n", params.vae_tiling ? "true" : "false");
    printf("    upscale_repeats:   %d\n", params.upscale_repeats);
}

void print_usage(int argc, const char* argv[]) {
    printf("usage: %s [arguments]\n", argv[0]);
    printf("\n");
    printf("arguments:\n");
    printf("  -h, --help                         show this help message and exit\n");
    printf("  -M, --mode [MODEL]                 run mode (txt2img or img2img or convert, default: txt2img)\n");
    printf("  -t, --threads N                    number of threads to use during computation (default: -1).\n");
    printf("                                     If threads <= 0, then threads will be set to the number of CPU physical cores\n");
    printf("  -m, --model [MODEL]                path to model\n");
    printf("  --vae [VAE]                        path to vae\n");
    printf("  --taesd [TAESD_PATH]               path to taesd. Using Tiny AutoEncoder for fast decoding (low quality)\n");
    printf("  --control-net [CONTROL_PATH]       path to control net model\n");
    printf("  --embd-dir [EMBEDDING_PATH]        path to embeddings.\n");
    printf("  --upscale-model [ESRGAN_PATH]      path to esrgan model. Upscale images after generate, just RealESRGAN_x4plus_anime_6B supported by now.\n");
    printf("  --upscale-repeats                  Run the ESRGAN upscaler this many times (default 1)\n");
    printf("  --type [TYPE]                      weight type (f32, f16, q4_0, q4_1, q5_0, q5_1, q8_0)\n");
    printf("                                     If not specified, the default is the type of the weight file.\n");
    printf("  --lora-model-dir [DIR]             lora model directory\n");
    printf("  -i, --init-img [IMAGE]             path to the input image, required by img2img\n");
    printf("  --control-image [IMAGE]            path to image condition, control net\n");
    printf("  -o, --output OUTPUT                path to write result image to (default: ./output.png)\n");
    printf("  -p, --prompt [PROMPT]              the prompt to render\n");
    printf("  -n, --negative-prompt PROMPT       the negative prompt (default: \"\")\n");
    printf("  --cfg-scale SCALE                  unconditional guidance scale: (default: 7.0)\n");
    printf("  --strength STRENGTH                strength for noising/unnoising (default: 0.75)\n");
    printf("  --control-strength STRENGTH        strength to apply Control Net (default: 0.9)\n");
    printf("                                     1.0 corresponds to full destruction of information in init image\n");
    printf("  -H, --height H                     image height, in pixel space (default: 512)\n");
    printf("  -W, --width W                      image width, in pixel space (default: 512)\n");
    printf("  --sampling-method {euler, euler_a, heun, dpm2, dpm++2s_a, dpm++2m, dpm++2mv2, lcm}\n");
    printf("                                     sampling method (default: \"euler_a\")\n");
    printf("  --steps  STEPS                     number of sample steps (default: 20)\n");
    printf("  --rng {std_default, cuda}          RNG (default: cuda)\n");
    printf("  -s SEED, --seed SEED               RNG seed (default: 42, use random seed for < 0)\n");
    printf("  -b, --batch-count COUNT            number of images to generate.\n");
    printf("  --schedule {discrete, karras}      Denoiser sigma schedule (default: discrete)\n");
    printf("  --clip-skip N                      ignore last layers of CLIP network; 1 ignores none, 2 ignores one layer (default: -1)\n");
    printf("                                     <= 0 represents unspecified, will be 1 for SD1.x, 2 for SD2.x\n");
    printf("  --vae-tiling                       process vae in tiles to reduce memory usage\n");
    printf("  --control-net-cpu                  keep controlnet in cpu (for low vram)\n");
    printf("  --canny                            apply canny preprocessor (edge detection)\n");
    printf("  --port                             the port for listening, default 8080\n");
    printf("  -v, --verbose                      print extra info\n");
}

void parse_args(int argc, const char** argv, SDParams& params) {
    bool invalid_arg = false;
    std::string arg;
    for (int i = 1; i < argc; i++) {
        arg = argv[i];

        if (arg == "-t" || arg == "--threads") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.n_threads = std::stoi(argv[i]);
        } else if (arg == "-M" || arg == "--mode") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            const char* mode_selected = argv[i];
            int mode_found            = -1;
            for (int d = 0; d < MODE_COUNT; d++) {
                if (!strcmp(mode_selected, modes_str[d])) {
                    mode_found = d;
                }
            }
            if (mode_found == -1) {
                fprintf(stderr,
                        "error: invalid mode %s, must be one of [txt2img, img2img, img2vid, convert]\n",
                        mode_selected);
                exit(1);
            }
            params.mode = (SDMode)mode_found;
        } else if (arg == "-m" || arg == "--model") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.model_path = argv[i];
        } else if (arg == "--vae") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.vae_path = argv[i];
        } else if (arg == "--taesd") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.taesd_path = argv[i];
        } else if (arg == "--control-net") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.controlnet_path = argv[i];
        } else if (arg == "--upscale-model") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.esrgan_path = argv[i];
        } else if (arg == "--embd-dir") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.embeddings_path = argv[i];
        } else if (arg == "--type") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            std::string type = argv[i];
            if (type == "f32") {
                params.wtype = SD_TYPE_F32;
            } else if (type == "f16") {
                params.wtype = SD_TYPE_F16;
            } else if (type == "q4_0") {
                params.wtype = SD_TYPE_Q4_0;
            } else if (type == "q4_1") {
                params.wtype = SD_TYPE_Q4_1;
            } else if (type == "q5_0") {
                params.wtype = SD_TYPE_Q5_0;
            } else if (type == "q5_1") {
                params.wtype = SD_TYPE_Q5_1;
            } else if (type == "q8_0") {
                params.wtype = SD_TYPE_Q8_0;
            } else {
                fprintf(stderr, "error: invalid weight format %s, must be one of [f32, f16, q4_0, q4_1, q5_0, q5_1, q8_0]\n",
                        type.c_str());
                exit(1);
            }
        } else if (arg == "--lora-model-dir") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.lora_model_dir = argv[i];
        } else if (arg == "-i" || arg == "--init-img") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.input_path = argv[i];
        } else if (arg == "--control-image") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.control_image_path = argv[i];
        } else if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.output_path = argv[i];
        } else if (arg == "-p" || arg == "--prompt") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.prompt = argv[i];
        } else if (arg == "--upscale-repeats") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.upscale_repeats = std::stoi(argv[i]);
            if (params.upscale_repeats < 1) {
                fprintf(stderr, "error: upscale multiplier must be at least 1\n");
                exit(1);
            }
        } else if (arg == "-n" || arg == "--negative-prompt") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.negative_prompt = argv[i];
        } else if (arg == "--cfg-scale") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.cfg_scale = std::stof(argv[i]);
        } else if (arg == "--strength") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.strength = std::stof(argv[i]);
        } else if (arg == "--control-strength") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.control_strength = std::stof(argv[i]);
        } else if (arg == "-H" || arg == "--height") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.height = std::stoi(argv[i]);
        } else if (arg == "-W" || arg == "--width") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.width = std::stoi(argv[i]);
        } else if (arg == "--steps") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.sample_steps = std::stoi(argv[i]);
        } else if (arg == "--clip-skip") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.clip_skip = std::stoi(argv[i]);
        } else if (arg == "--vae-tiling") {
            params.vae_tiling = true;
        } else if (arg == "--control-net-cpu") {
            params.control_net_cpu = true;
        } else if (arg == "--canny") {
            params.canny_preprocess = true;
        } else if (arg == "-b" || arg == "--batch-count") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.batch_count = std::stoi(argv[i]);
        } else if (arg == "--rng") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            std::string rng_type_str = argv[i];
            if (rng_type_str == "std_default") {
                params.rng_type = STD_DEFAULT_RNG;
            } else if (rng_type_str == "cuda") {
                params.rng_type = CUDA_RNG;
            } else {
                invalid_arg = true;
                break;
            }
        } else if (arg == "--schedule") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            const char* schedule_selected = argv[i];
            int schedule_found            = -1;
            for (int d = 0; d < N_SCHEDULES; d++) {
                if (!strcmp(schedule_selected, schedule_str[d])) {
                    schedule_found = d;
                }
            }
            if (schedule_found == -1) {
                invalid_arg = true;
                break;
            }
            params.schedule = (schedule_t)schedule_found;
        } else if (arg == "-s" || arg == "--seed") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.seed = std::stoll(argv[i]);
        } else if (arg == "--sampling-method") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            const char* sample_method_selected = argv[i];
            int sample_method_found            = -1;
            for (int m = 0; m < N_SAMPLE_METHODS; m++) {
                if (!strcmp(sample_method_selected, sample_method_str[m])) {
                    sample_method_found = m;
                }
            }
            if (sample_method_found == -1) {
                invalid_arg = true;
                break;
            }
            params.sample_method = (sample_method_t)sample_method_found;
        } else if (arg == "--port") {
            if (++i >= argc) {
                invalid_arg = true;
                break;
            }
            params.port = std::stoi(argv[i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argc, argv);
            exit(0);
        } else if (arg == "-v" || arg == "--verbose") {
            params.verbose = true;
        } else {
            fprintf(stderr, "error: unknown argument: %s\n", arg.c_str());
            print_usage(argc, argv);
            exit(1);
        }
    }
    if (invalid_arg) {
        fprintf(stderr, "error: invalid parameter for argument: %s\n", arg.c_str());
        print_usage(argc, argv);
        exit(1);
    }
    if (params.n_threads <= 0) {
        params.n_threads = get_num_physical_cores();
    }

    if (params.model_path.length() == 0) {
        fprintf(stderr, "error: the following arguments are required: model_path\n");
        print_usage(argc, argv);
        exit(1);
    }

    if ((params.mode == IMG2IMG || params.mode == IMG2VID) && params.input_path.length() == 0) {
        fprintf(stderr, "error: when using the img2img mode, the following arguments are required: init-img\n");
        print_usage(argc, argv);
        exit(1);
    }

    if (params.output_path.length() == 0) {
        fprintf(stderr, "error: the following arguments are required: output_path\n");
        print_usage(argc, argv);
        exit(1);
    }

    if (params.width <= 0 || params.width % 64 != 0) {
        fprintf(stderr, "error: the width must be a multiple of 64\n");
        exit(1);
    }

    if (params.height <= 0 || params.height % 64 != 0) {
        fprintf(stderr, "error: the height must be a multiple of 64\n");
        exit(1);
    }

    if (params.sample_steps <= 0) {
        fprintf(stderr, "error: the sample_steps must be greater than 0\n");
        exit(1);
    }

    if (params.strength < 0.f || params.strength > 1.f) {
        fprintf(stderr, "error: can only work with strength in [0.0, 1.0]\n");
        exit(1);
    }

    if (params.seed < 0) {
        srand((int)time(NULL));
        params.seed = rand();
    }

    if (params.mode == CONVERT) {
        if (params.output_path == "output.png") {
            params.output_path = "output.gguf";
        }
    }
}

static std::string sd_basename(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    pos = path.find_last_of('\\');
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

std::string get_image_params(SDParams params, int64_t seed) {
    std::string parameter_string = params.prompt + "\n";
    if (params.negative_prompt.size() != 0) {
        parameter_string += "Negative prompt: " + params.negative_prompt + "\n";
    }
    parameter_string += "Steps: " + std::to_string(params.sample_steps) + ", ";
    parameter_string += "CFG scale: " + std::to_string(params.cfg_scale) + ", ";
    parameter_string += "Seed: " + std::to_string(seed) + ", ";
    parameter_string += "Size: " + std::to_string(params.width) + "x" + std::to_string(params.height) + ", ";
    parameter_string += "Model: " + sd_basename(params.model_path) + ", ";
    parameter_string += "RNG: " + std::string(rng_type_to_str[params.rng_type]) + ", ";
    parameter_string += "Sampler: " + std::string(sample_method_str[params.sample_method]);
    if (params.schedule == KARRAS) {
        parameter_string += " karras";
    }
    parameter_string += ", ";
    parameter_string += "Version: stable-diffusion.cpp";
    return parameter_string;
}

void sd_log_cb(enum sd_log_level_t level, const char* log, void* data) {
    SDParams* params = (SDParams*)data;
    if (!params->verbose && level <= SD_LOG_DEBUG) {
        return;
    }
    if (level <= SD_LOG_INFO) {
        fputs(log, stdout);
        fflush(stdout);
    } else {
        fputs(log, stderr);
        fflush(stderr);
    }
}
class sd_http_server {
private:
    /* data */
public:
    sd_http_server(/* args */);
    ~sd_http_server();

    void run(sd_ctx_t* sd_ctx, const SDParams& params);
};

sd_http_server::sd_http_server(/* args */) {
}

sd_http_server::~sd_http_server() {
}

struct ResponseContent {
    std::string status;
    double generationTime = 0.0;
    int id                = 1;
    std::vector<std::string> output;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ResponseContent, status, id, output)
};

void sd_http_server::run(sd_ctx_t* sd_ctx, const SDParams& sd_params) {
    // HTTP
    httplib::Server svr;

    svr.set_default_headers({{"Server", "llama.cpp"}});
    svr.Get("/txt2img", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("Hello World!", "text/plain");
    });

    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string message;
        try {
            std::rethrow_exception(std::move(ep));
        } catch (std::exception& e) {
            message = e.what();
        } catch (...) {
            message = "Unknown Exception";
        }

        json formatted_error = format_error_response(message, ERROR_TYPE_SERVER);
        // LOG_VERBOSE("Got exception", formatted_error);

        json final_response{{"error", formatted_error}};
        res.set_content(final_response.dump(), "application/json; charset=utf-8");
        res.status = json_value(formatted_error, "code", 500);
    });

    // set timeouts and change hostname and port
    svr.set_read_timeout(30);
    svr.set_write_timeout(30);

    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 9999999);

    svr.Post("/txt2img", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));

        SDParams params = sd_params;
        json data       = json::parse(req.body);

        params.prompt = data.at("prompt");

        if (data.contains("negative_prompt"))
            data.at("negative_prompt").get_to(params.negative_prompt);

        if (data.contains("steps"))
            data.at("steps").get_to(params.sample_steps);

        if (data.contains("seed"))
            data.at("seed").get_to(params.seed);
        else {
            srand(time(nullptr));
            params.seed = dist(rd);
        }

        sd_image_t* control_image = nullptr;

        sd_image_t* results = txt2img(sd_ctx,
                                      params.prompt.c_str(),
                                      params.negative_prompt.c_str(),
                                      params.clip_skip,
                                      params.cfg_scale,
                                      params.width,
                                      params.height,
                                      params.sample_method,
                                      params.sample_steps,
                                      params.seed,
                                      params.batch_count,
                                      control_image,
                                      params.control_strength,
                                      params.style_ratio,
                                      params.normalize_input,
                                      params.input_id_images_path.c_str());

        std::vector<std::string> output;

        for (size_t i = 0; i < params.batch_count; i++) {
            auto img_name = params.output_path + std::to_string(params.hash()) + ".png";

            stbi_write_png(img_name.c_str(),
                           results[i].width, results[i].height, results[i].channel,
                           results[i].data, 0, get_image_params(params, params.seed + i).c_str());

            output.push_back(img_name);
        }

        ResponseContent content;
        content.status = "success";
        content.output = move(output);
        json jcontent  = content;

        res.set_content(json(content).dump(), "application/json");
    });

    auto ret = svr.set_mount_point("/generations", "./shared");
    if (!ret) {
        throw std::runtime_error("the folder 'share' doest not exist.");
    }

    // The handler is called right before the response is sent to a client
    // svr.set_file_request_handler([](const httplib::Request& req, httplib::Response& res) {
    //     std::cout << "call file request handler" << std::endl;

    // });

    printf("sd http server is running at host %s port %d\n", sd_params.host.c_str(), sd_params.port);

    svr.listen(sd_params.host, sd_params.port);
}

int main(int argc, const char* argv[]) {
    SDParams params;
    parse_args(argc, argv, params);

    sd_set_log_callback(sd_log_cb, (void*)&params);

    if (params.verbose) {
        print_params(params);
        printf("%s", sd_get_system_info());
    }

    if (params.mode == CONVERT) {
        bool success = convert(params.model_path.c_str(), params.vae_path.c_str(), params.output_path.c_str(), params.wtype);
        if (!success) {
            fprintf(stderr,
                    "convert '%s'/'%s' to '%s' failed\n",
                    params.model_path.c_str(),
                    params.vae_path.c_str(),
                    params.output_path.c_str());
            return 1;
        } else {
            printf("convert '%s'/'%s' to '%s' success\n",
                   params.model_path.c_str(),
                   params.vae_path.c_str(),
                   params.output_path.c_str());
            return 0;
        }
    }

    if (params.mode == IMG2VID) {
        fprintf(stderr, "SVD support is broken, do not use it!!!\n");
        return 1;
    }

    bool vae_decode_only        = true;
    uint8_t* input_image_buffer = NULL;
    if (params.mode == IMG2IMG || params.mode == IMG2VID) {
        vae_decode_only = false;

        int c              = 0;
        input_image_buffer = stbi_load(params.input_path.c_str(), &params.width, &params.height, &c, 3);
        if (input_image_buffer == NULL) {
            fprintf(stderr, "load image from '%s' failed\n", params.input_path.c_str());
            return 1;
        }
        if (c != 3) {
            fprintf(stderr, "input image must be a 3 channels RGB image, but got %d channels\n", c);
            free(input_image_buffer);
            return 1;
        }
        if (params.width <= 0 || params.width % 64 != 0) {
            fprintf(stderr, "error: the width of image must be a multiple of 64\n");
            free(input_image_buffer);
            return 1;
        }
        if (params.height <= 0 || params.height % 64 != 0) {
            fprintf(stderr, "error: the height of image must be a multiple of 64\n");
            free(input_image_buffer);
            return 1;
        }
    }

    sd_ctx_t* sd_ctx = new_sd_ctx(params.model_path.c_str(),
                                  params.vae_path.c_str(),
                                  params.taesd_path.c_str(),
                                  params.controlnet_path.c_str(),
                                  params.lora_model_dir.c_str(),
                                  params.embeddings_path.c_str(),
                                  params.stacked_id_embeddings_path.c_str(),
                                  vae_decode_only,
                                  params.vae_tiling,
                                  true,
                                  params.n_threads,
                                  params.wtype,
                                  params.rng_type,
                                  params.schedule,
                                  params.clip_on_cpu,
                                  params.control_net_cpu,
                                  params.vae_on_cpu);

    if (sd_ctx == NULL) {
        printf("new_sd_ctx_t failed\n");
        return 1;
    }

    try {
        sd_http_server server;

        server.run(sd_ctx, params);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }

    free_sd_ctx(sd_ctx);

    return 0;
}
