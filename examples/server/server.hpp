#pragma once

#include <set>
#include <map>
#include <vector>

#include "utils.hpp"
#include "json.hpp"
using json = nlohmann::json;

enum stop_type {
    STOP_TYPE_FULL,
    STOP_TYPE_PARTIAL,
};

enum slot_state {
    SLOT_STATE_IDLE,
    SLOT_STATE_PROCESSING,
};

enum slot_command {
    SLOT_COMMAND_NONE,
    SLOT_COMMAND_LOAD_PROMPT,
    SLOT_COMMAND_RELEASE,
};

enum server_state {
    SERVER_STATE_LOADING_MODEL,  // Server is starting up, model not fully loaded yet
    SERVER_STATE_READY,          // Server is ready and model is loaded
    SERVER_STATE_ERROR           // An error occurred, load_model failed
};

enum server_task_type {
    SERVER_TASK_TYPE_COMPLETION,
    SERVER_TASK_TYPE_CANCEL,
    SERVER_TASK_TYPE_NEXT_RESPONSE,
    SERVER_TASK_TYPE_METRICS,
    SERVER_TASK_TYPE_SLOT_SAVE,
    SERVER_TASK_TYPE_SLOT_RESTORE,
    SERVER_TASK_TYPE_SLOT_ERASE,
};

struct server_task {
    int id        = -1; // to be filled by server_queue
    int id_multi  = -1;
    int id_target = -1;

    server_task_type type;
    json data;

    bool infill    = false;
    bool embedding = false;
};

struct server_task_result {
    int id       = -1;
    int id_multi = -1;

    json data;

    bool stop;
    bool error;
};

struct server_task_multi {
    int id = -1;

    std::set<int> subtasks_remaining;
    std::vector<server_task_result> results;
};

struct slot_params {
    bool stream       = true;
    bool cache_prompt = false; // remember the prompt to avoid reprocessing all prompt

    uint32_t seed      = -1; // RNG seed
    int32_t  n_keep    =  0; // number of tokens to keep from initial prompt
    int32_t  n_discard =  0; // number of tokens after n_keep that may be discarded when shifting context, 0 defaults to half
    int32_t  n_predict = -1; // new tokens to predict

    std::vector<std::string> antiprompt;

    json input_prefix;
    json input_suffix;
};

struct server_params {
    int32_t port           = 8080;
    int32_t read_timeout   = 600;
    int32_t write_timeout  = 600;
    int32_t n_threads_http = -1;

    std::string hostname      = "127.0.0.1";
    std::string public_path   = "";
    std::string chat_template = "";
    std::string system_prompt = "";

    std::vector<std::string> api_keys;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    std::string ssl_key_file = "";
    std::string ssl_cert_file = "";
#endif

    bool slots_endpoint   = true;
    bool metrics_endpoint = false;
    std::string slot_save_path;
};

// struct server_slot {
//     int id;
//     int id_task = -1;
//     int id_multi = -1;

//     struct slot_params params;

//     slot_state state = SLOT_STATE_IDLE;
//     slot_command command = SLOT_COMMAND_NONE;

//     // used to determine the slot that has been used the longest
//     int64_t t_last_used = -1;

//     // generation props
//     int32_t n_ctx       = 0;  // context size per slot
//     int32_t n_past      = 0;
//     int32_t n_decoded   = 0;
//     int32_t n_remaining = -1;
//     int32_t i_batch     = -1;
//     int32_t n_predict   = -1; // TODO: disambiguate from params.n_predict

//     int32_t n_prompt_tokens           = 0;
//     int32_t n_prompt_tokens_processed = 0;

//     json prompt;

//     // when a task is submitted, we first tokenize the prompt and store it here
//     std::vector<llama_token> prompt_tokens;

//     std::string generated_text;
//     std::vector<llama_token> cache_tokens;
//     std::vector<completion_token_output> generated_token_probs;

//     bool infill         = false;
//     bool embedding      = false;
//     bool has_next_token = true;
//     bool truncated      = false;
//     bool stopped_eos    = false;
//     bool stopped_word   = false;
//     bool stopped_limit  = false;

//     bool oaicompat = false;

//     std::string oaicompat_model;
//     std::string stopping_word;

//     // sampling
//     llama_token sampled;
//     struct llama_sampling_params sparams;
//     llama_sampling_context * ctx_sampling = nullptr;
//     json json_schema;

//     int32_t ga_i = 0;   // group-attention state
//     int32_t ga_n = 1;   // group-attention factor
//     int32_t ga_w = 512; // group-attention width

//     int32_t n_past_se = 0; // self-extend

//     // stats
//     size_t n_sent_text = 0; // number of sent text character
//     size_t n_sent_token_probs = 0;

//     int64_t t_start_process_prompt;
//     int64_t t_start_generation;

//     double t_prompt_processing; // ms
//     double t_token_generation; // ms

//     void reset() {
//         n_prompt_tokens    = 0;
//         generated_text     = "";
//         truncated          = false;
//         stopped_eos        = false;
//         stopped_word       = false;
//         stopped_limit      = false;
//         stopping_word      = "";
//         n_past             = 0;
//         n_sent_text        = 0;
//         n_sent_token_probs = 0;
//         infill             = false;
//         ga_i               = 0;
//         n_past_se          = 0;

//         generated_token_probs.clear();
//     }

//     bool has_budget(gpt_params &global_params) {
//         if (params.n_predict == -1 && global_params.n_predict == -1) {
//             return true; // limitless
//         }

//         n_remaining = -1;

//         if (params.n_predict != -1) {
//             n_remaining = params.n_predict - n_decoded;
//         } else if (global_params.n_predict != -1) {
//             n_remaining = global_params.n_predict - n_decoded;
//         }

//         return n_remaining > 0; // no budget
//     }

//     bool available() const {
//         return state == SLOT_STATE_IDLE && command == SLOT_COMMAND_NONE;
//     }

//     bool is_processing() const {
//         return (state == SLOT_STATE_IDLE && command == SLOT_COMMAND_LOAD_PROMPT) || state == SLOT_STATE_PROCESSING;
//     }

//     void add_token_string(const completion_token_output & token) {
//         if (command == SLOT_COMMAND_RELEASE) {
//             return;
//         }
//         generated_token_probs.push_back(token);
//     }

//     void release() {
//         if (state == SLOT_STATE_PROCESSING) {
//             t_token_generation = (ggml_time_us() - t_start_generation) / 1e3;
//             command = SLOT_COMMAND_RELEASE;
//         }
//     }

//     json get_formated_timings() const {
//         return json {
//             {"prompt_n",               n_prompt_tokens_processed},
//             {"prompt_ms",              t_prompt_processing},
//             {"prompt_per_token_ms",    t_prompt_processing / n_prompt_tokens_processed},
//             {"prompt_per_second",      1e3 / t_prompt_processing * n_prompt_tokens_processed},

//             {"predicted_n",            n_decoded},
//             {"predicted_ms",           t_token_generation},
//             {"predicted_per_token_ms", t_token_generation / n_decoded},
//             {"predicted_per_second",   1e3 / t_token_generation * n_decoded},
//         };
//     }

//     size_t find_stopping_strings(const std::string & text, const size_t last_token_size, const stop_type type) {
//         size_t stop_pos = std::string::npos;

//         for (const std::string & word : params.antiprompt) {
//             size_t pos;

//             if (type == STOP_TYPE_FULL) {
//                 const size_t tmp      = word.size() + last_token_size;
//                 const size_t from_pos = text.size() > tmp ? text.size() - tmp : 0;

//                 pos = text.find(word, from_pos);
//             } else {
//                 pos = find_partial_stop_string(word, text);
//             }

//             if (pos != std::string::npos && (stop_pos == std::string::npos || pos < stop_pos)) {
//                 if (type == STOP_TYPE_FULL) {
//                     stopped_word   = true;
//                     stopping_word  = word;
//                     has_next_token = false;
//                 }
//                 stop_pos = pos;
//             }
//         }

//         return stop_pos;
//     }

//     void print_timings() const {
//         char buffer[512];

//         double t_token = t_prompt_processing / n_prompt_tokens_processed;
//         double n_tokens_second = 1e3 / t_prompt_processing * n_prompt_tokens_processed;

//         snprintf(buffer, 512, "prompt eval time     = %10.2f ms / %5d tokens (%8.2f ms per token, %8.2f tokens per second)",
//                 t_prompt_processing, n_prompt_tokens_processed,
//                 t_token, n_tokens_second);

//         LOG_INFO(buffer, {
//             {"id_slot",                   id},
//             {"id_task",                   id_task},
//             {"t_prompt_processing",       t_prompt_processing},
//             {"n_prompt_tokens_processed", n_prompt_tokens_processed},
//             {"t_token",                   t_token},
//             {"n_tokens_second",           n_tokens_second},
//         });

//         t_token = t_token_generation / n_decoded;
//         n_tokens_second = 1e3 / t_token_generation * n_decoded;

//         snprintf(buffer, 512, "generation eval time = %10.2f ms / %5d runs   (%8.2f ms per token, %8.2f tokens per second)",
//                 t_token_generation, n_decoded,
//                 t_token, n_tokens_second);

//         LOG_INFO(buffer, {
//             {"id_slot",            id},
//             {"id_task",            id_task},
//             {"t_token_generation", t_token_generation},
//             {"n_decoded",          n_decoded},
//             {"t_token",            t_token},
//             {"n_tokens_second",    n_tokens_second},
//         });

//         snprintf(buffer, 512, "          total time = %10.2f ms", t_prompt_processing + t_token_generation);

//         LOG_INFO(buffer, {
//             {"id_slot",             id},
//             {"id_task",             id_task},
//             {"t_prompt_processing", t_prompt_processing},
//             {"t_token_generation",  t_token_generation},
//             {"t_total",             t_prompt_processing + t_token_generation},
//         });
//     }
// };

// struct server_metrics {
//     int64_t t_start = 0;

//     uint64_t n_prompt_tokens_processed_total = 0;
//     uint64_t t_prompt_processing_total       = 0;
//     uint64_t n_tokens_predicted_total        = 0;
//     uint64_t t_tokens_generation_total       = 0;

//     uint64_t n_prompt_tokens_processed = 0;
//     uint64_t t_prompt_processing       = 0;

//     uint64_t n_tokens_predicted  = 0;
//     uint64_t t_tokens_generation = 0;

//     void init() {
//         t_start = ggml_time_us();
//     }

//     void on_prompt_eval(const server_slot & slot) {
//         n_prompt_tokens_processed_total += slot.n_prompt_tokens_processed;
//         n_prompt_tokens_processed       += slot.n_prompt_tokens_processed;
//         t_prompt_processing             += slot.t_prompt_processing;
//         t_prompt_processing_total       += slot.t_prompt_processing;
//     }

//     void on_prediction(const server_slot & slot) {
//         n_tokens_predicted_total   += slot.n_decoded;
//         n_tokens_predicted         += slot.n_decoded;
//         t_tokens_generation        += slot.t_token_generation;
//         t_tokens_generation_total  += slot.t_token_generation;
//     }

//     void reset_bucket() {
//         n_prompt_tokens_processed = 0;
//         t_prompt_processing       = 0;
//         n_tokens_predicted        = 0;
//         t_tokens_generation       = 0;
//     }
// };

struct server_queue {
    int id = 0;
    bool running;

    // queues
    std::vector<server_task> queue_tasks;
    std::vector<server_task> queue_tasks_deferred;

    std::vector<server_task_multi> queue_multitasks;

    std::mutex mutex_tasks;
    std::condition_variable condition_tasks;

    // callback functions
    std::function<void(server_task       &)> callback_new_task;
    std::function<void(server_task_multi &)> callback_finish_multitask;
    std::function<void(void)>                callback_update_slots;

    // Add a new task to the end of the queue
    int post(server_task task) {
        std::unique_lock<std::mutex> lock(mutex_tasks);
        if (task.id == -1) {
            task.id = id++;
            LOG_VERBOSE("new task id", {{"new_id", task.id}});
        }
        queue_tasks.push_back(std::move(task));
        condition_tasks.notify_one();
        return task.id;
    }

    // Add a new task, but defer until one slot is available
    void defer(server_task task) {
        std::unique_lock<std::mutex> lock(mutex_tasks);
        queue_tasks_deferred.push_back(std::move(task));
    }

    // Get the next id for creating anew task
    int get_new_id() {
        std::unique_lock<std::mutex> lock(mutex_tasks);
        int new_id = id++;
        LOG_VERBOSE("new task id", {{"new_id", new_id}});
        return new_id;
    }

    // Register function to process a new task
    void on_new_task(std::function<void(server_task &)> callback) {
        callback_new_task = std::move(callback);
    }

    // Register function to process a multitask when it is finished
    void on_finish_multitask(std::function<void(server_task_multi&)> callback) {
        callback_finish_multitask = std::move(callback);
    }

    // Register the function to be called when all slots data is ready to be processed
    void on_update_slots(std::function<void(void)> callback) {
        callback_update_slots = std::move(callback);
    }

    // Call when the state of one slot is changed
    void notify_slot_changed() {
        // move deferred tasks back to main loop
        std::unique_lock<std::mutex> lock(mutex_tasks);
        for (auto & task : queue_tasks_deferred) {
            queue_tasks.push_back(std::move(task));
        }
        queue_tasks_deferred.clear();
    }

    // end the start_loop routine
    void terminate() {
        std::unique_lock<std::mutex> lock(mutex_tasks);
        running = false;
        condition_tasks.notify_all();
    }

    /**
     * Main loop consists of these steps:
     * - Wait until a new task arrives
     * - Process the task (i.e. maybe copy data into slot)
     * - Check if multitask is finished
     * - Update all slots
     */
    void start_loop() {
        running = true;

        while (true) {
            LOG_VERBOSE("new task may arrive", {});

            while (true) {
                std::unique_lock<std::mutex> lock(mutex_tasks);
                if (queue_tasks.empty()) {
                    lock.unlock();
                    break;
                }
                server_task task = queue_tasks.front();
                queue_tasks.erase(queue_tasks.begin());
                lock.unlock();
                LOG_VERBOSE("callback_new_task", {{"id_task", task.id}});
                callback_new_task(task);
            }

            LOG_VERBOSE("update_multitasks", {});

            // check if we have any finished multitasks
            auto queue_iterator = queue_multitasks.begin();
            while (queue_iterator != queue_multitasks.end()) {
                if (queue_iterator->subtasks_remaining.empty()) {
                    // all subtasks done == multitask is done
                    server_task_multi current_multitask = *queue_iterator;
                    callback_finish_multitask(current_multitask);
                    // remove this multitask
                    queue_iterator = queue_multitasks.erase(queue_iterator);
                } else {
                    ++queue_iterator;
                }
            }

            // all tasks in the current loop is processed, slots data is now ready
            LOG_VERBOSE("callback_update_slots", {});

            callback_update_slots();

            LOG_VERBOSE("wait for new task", {});
            {
                std::unique_lock<std::mutex> lock(mutex_tasks);
                if (queue_tasks.empty()) {
                    if (!running) {
                        LOG_VERBOSE("ending start_loop", {});
                        return;
                    }
                    condition_tasks.wait(lock, [&]{
                        return (!queue_tasks.empty() || !running);
                    });
                }
            }
        }
    }

    //
    // functions to manage multitasks
    //

    // add a multitask by specifying the id of all subtask (subtask is a server_task)
    void add_multitask(int id_multi, std::vector<int> & sub_ids) {
        std::lock_guard<std::mutex> lock(mutex_tasks);
        server_task_multi multi;
        multi.id = id_multi;
        std::copy(sub_ids.begin(), sub_ids.end(), std::inserter(multi.subtasks_remaining, multi.subtasks_remaining.end()));
        queue_multitasks.push_back(multi);
    }

    // updatethe remaining subtasks, while appending results to multitask
    void update_multitask(int id_multi, int id_sub, server_task_result & result) {
        std::lock_guard<std::mutex> lock(mutex_tasks);
        for (auto & multitask : queue_multitasks) {
            if (multitask.id == id_multi) {
                multitask.subtasks_remaining.erase(id_sub);
                multitask.results.push_back(result);
            }
        }
    }
};

struct server_response {
    typedef std::function<void(int, int, server_task_result &)> callback_multitask_t;
    callback_multitask_t callback_update_multitask;

    // for keeping track of all tasks waiting for the result
    std::set<int> waiting_task_ids;

    // the main result queue
    std::vector<server_task_result> queue_results;

    std::mutex mutex_results;
    std::condition_variable condition_results;

    // add the id_task to the list of tasks waiting for response
    void add_waiting_task_id(int id_task) {
        LOG_VERBOSE("waiting for task id", {{"id_task", id_task}});

        std::unique_lock<std::mutex> lock(mutex_results);
        waiting_task_ids.insert(id_task);
    }

    // when the request is finished, we can remove task associated with it
    void remove_waiting_task_id(int id_task) {
        LOG_VERBOSE("remove waiting for task id", {{"id_task", id_task}});

        std::unique_lock<std::mutex> lock(mutex_results);
        waiting_task_ids.erase(id_task);
    }

    // This function blocks the thread until there is a response for this id_task
    server_task_result recv(int id_task) {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_results);
            condition_results.wait(lock, [&]{
                return !queue_results.empty();
            });

            for (int i = 0; i < (int) queue_results.size(); i++) {
                if (queue_results[i].id == id_task) {
                    assert(queue_results[i].id_multi == -1);
                    server_task_result res = queue_results[i];
                    queue_results.erase(queue_results.begin() + i);
                    return res;
                }
            }
        }

        // should never reach here
    }

    // Register the function to update multitask
    void on_multitask_update(callback_multitask_t callback) {
        callback_update_multitask = std::move(callback);
    }

    // Send a new result to a waiting id_task
    void send(server_task_result result) {
        LOG_VERBOSE("send new result", {{"id_task", result.id}});

        std::unique_lock<std::mutex> lock(mutex_results);
        for (const auto & id_task : waiting_task_ids) {
            // LOG_TEE("waiting task id %i \n", id_task);
            // for now, tasks that have associated parent multitasks just get erased once multitask picks up the result
            if (result.id_multi == id_task) {
                LOG_VERBOSE("callback_update_multitask", {{"id_task", id_task}});
                callback_update_multitask(id_task, result.id, result);
                continue;
            }

            if (result.id == id_task) {
                LOG_VERBOSE("queue_results.push_back", {{"id_task", id_task}});
                queue_results.push_back(result);
                condition_results.notify_all();
                return;
            }
        }
    }
};
