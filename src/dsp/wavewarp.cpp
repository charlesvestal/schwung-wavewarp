#include "plugin_api_v1.h"

#include "JuceHeader.h"
#include "load_save.h"
#include "sound_engine.h"
#include "synth_base.h"
#include "synth_constants.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

const host_api_v1_t* g_host = nullptr;

void log_message(const char* fmt, ...) {
  if (g_host == nullptr || g_host->log == nullptr)
    return;

  char message[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(message, sizeof(message), fmt, args);
  va_end(args);

  char prefixed[560];
  snprintf(prefixed, sizeof(prefixed), "[wavewarp] %s", message);
  g_host->log(prefixed);
}

float clamp_sample(float value) {
  if (value < -1.0f)
    return -1.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

struct ParamBinding {
  const char* module_key;
  const char* vital_key;
  bool integer_value;
};

static const ParamBinding kBindings[] = {
  {"cutoff", "filter_1_cutoff", false},
  {"resonance", "filter_1_resonance", false},
  {"attack", "env_1_attack", false},
  {"decay", "env_1_decay", false},
  {"sustain", "env_1_sustain", false},
  {"release", "env_1_release", false},
  {"voice_level", "voice_amplitude", false},
  {"transpose", "voice_transpose", true},
  {"tune", "voice_tune", false},
  {"polyphony", "polyphony", true},
  {"portamento", "portamento_time", false},
  {"macro1", "macro_control_1", false},
  {"macro2", "macro_control_2", false},
  {"macro3", "macro_control_3", false},
  {"macro4", "macro_control_4", false},
  {"osc1_level", "osc_1_level", false},
  {"osc1_frame", "osc_1_wave_frame", true},
  {"osc1_transpose", "osc_1_transpose", true},
  {"osc1_tune", "osc_1_tune", false},
  {"osc1_unison", "osc_1_unison_voices", true},
  {"osc1_detune", "osc_1_unison_detune", false},
  {"osc1_shape", "osc_1_distortion_type", true},
  {"osc1_shape_amt", "osc_1_distortion_amount", false},
  {"osc2_level", "osc_2_level", false},
  {"osc2_frame", "osc_2_wave_frame", true},
  {"osc2_transpose", "osc_2_transpose", true},
  {"osc2_tune", "osc_2_tune", false},
  {"osc2_unison", "osc_2_unison_voices", true},
  {"osc2_detune", "osc_2_unison_detune", false},
  {"osc2_shape", "osc_2_distortion_type", true},
  {"osc2_shape_amt", "osc_2_distortion_amount", false},
  {"sub_level", "sub_level", false},
  {"chorus_on", "chorus_on", true},
  {"chorus_mix", "chorus_dry_wet", false},
  {"reverb_on", "reverb_on", true},
  {"reverb_mix", "reverb_dry_wet", false},
};

static const char* kChainParamsJson = R"json([
  {"key":"cutoff","name":"Cutoff","type":"float","min":8.0,"max":136.0,"default":60.0,"step":2.0,"unit":"st"},
  {"key":"resonance","name":"Resonance","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.01},
  {"key":"attack","name":"Attack","type":"float","min":0.0,"max":2.37842,"default":0.1495,"step":0.01,"unit":"s"},
  {"key":"decay","name":"Decay","type":"float","min":0.0,"max":2.37842,"default":1.0,"step":0.01,"unit":"s"},
  {"key":"sustain","name":"Sustain","type":"float","min":0.0,"max":1.0,"default":1.0,"step":0.01},
  {"key":"release","name":"Release","type":"float","min":0.0,"max":2.37842,"default":0.5476,"step":0.01,"unit":"s"},
  {"key":"osc1_shape_amt","name":"Shape Amt","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
  {"key":"osc2_level","name":"Osc 2 Level","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
  {"key":"osc1_level","name":"Osc 1 Level","type":"float","min":0.0,"max":1.0,"default":0.7071,"step":0.02},
  {"key":"osc1_frame","name":"Wave Frame","type":"int","min":0,"max":256,"default":0,"step":1},
  {"key":"osc1_shape","name":"Shape Type","type":"enum","options":["None","Sync","Formant","Quantize","Bend","Squeeze","Pulse","FM <- Osc","FM <- Osc","FM <- Sample","RM <- Osc","RM <- Osc","RM <- Sample"],"default":0},
  {"key":"osc1_transpose","name":"Osc 1 Transpose","type":"int","min":-48,"max":48,"default":0},
  {"key":"osc1_tune","name":"Osc 1 Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
  {"key":"osc1_unison","name":"Osc 1 Unison","type":"int","min":1,"max":16,"default":1},
  {"key":"osc1_detune","name":"Osc 1 Detune","type":"float","min":0.0,"max":10.0,"default":4.4721,"step":0.05},
  {"key":"osc2_frame","name":"Osc 2 Frame","type":"int","min":0,"max":256,"default":0,"step":1},
  {"key":"osc2_shape","name":"Osc 2 Shape","type":"enum","options":["None","Sync","Formant","Quantize","Bend","Squeeze","Pulse","FM <- Osc","FM <- Osc","FM <- Sample","RM <- Osc","RM <- Osc","RM <- Sample"],"default":0},
  {"key":"osc2_shape_amt","name":"Osc 2 Shape Amt","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
  {"key":"osc2_transpose","name":"Osc 2 Transpose","type":"int","min":-48,"max":48,"default":0},
  {"key":"osc2_tune","name":"Osc 2 Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
  {"key":"osc2_unison","name":"Osc 2 Unison","type":"int","min":1,"max":16,"default":1},
  {"key":"osc2_detune","name":"Osc 2 Detune","type":"float","min":0.0,"max":10.0,"default":4.4721,"step":0.05},
  {"key":"voice_level","name":"Voice Level","type":"float","min":0.0,"max":1.0,"default":1.0,"step":0.02},
  {"key":"transpose","name":"Transpose","type":"int","min":-48,"max":48,"default":0},
  {"key":"tune","name":"Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
  {"key":"polyphony","name":"Polyphony","type":"int","min":1,"max":32,"default":8},
  {"key":"portamento","name":"Portamento","type":"float","min":-10.0,"max":4.0,"default":-10.0,"step":0.1,"unit":"s"},
  {"key":"sub_level","name":"Sub Level","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
  {"key":"chorus_on","name":"Chorus On","type":"enum","options":["Off","On"],"default":0},
  {"key":"chorus_mix","name":"Chorus Mix","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
  {"key":"reverb_on","name":"Reverb On","type":"enum","options":["Off","On"],"default":0},
  {"key":"reverb_mix","name":"Reverb Mix","type":"float","min":0.0,"max":1.0,"default":0.25,"step":0.02},
  {"key":"macro1","name":"Macro 1","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
  {"key":"macro2","name":"Macro 2","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
  {"key":"macro3","name":"Macro 3","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
  {"key":"macro4","name":"Macro 4","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02}
])json";

static const char* kUiHierarchyJson = R"json({
  "levels": {
    "root": {
      "label": "WaveWarp",
      "params": [
        {"level":"presets","label":"Presets"},
        {"key":"cutoff","label":"Cutoff","type":"float","min":8.0,"max":136.0,"default":60.0,"step":2.0,"unit":"st"},
        {"key":"resonance","label":"Resonance","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.01},
        {"key":"attack","label":"Attack","type":"float","min":0.0,"max":2.37842,"default":0.1495,"step":0.01,"unit":"s"},
        {"key":"decay","label":"Decay","type":"float","min":0.0,"max":2.37842,"default":1.0,"step":0.01,"unit":"s"},
        {"key":"sustain","label":"Sustain","type":"float","min":0.0,"max":1.0,"default":1.0,"step":0.01},
        {"key":"release","label":"Release","type":"float","min":0.0,"max":2.37842,"default":0.5476,"step":0.01,"unit":"s"},
        {"key":"osc1_shape_amt","label":"Shape Amt","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
        {"key":"osc2_level","label":"Osc 2 Lvl","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
        {"level":"osc1","label":"Osc 1"},
        {"level":"osc2","label":"Osc 2"},
        {"level":"mix","label":"Mix"},
        {"level":"voice","label":"Voice"},
        {"level":"fx","label":"FX"}
      ],
      "knobs": ["cutoff","resonance","attack","decay","sustain","release","osc1_shape_amt","osc2_level"]
    },
    "presets": {
      "label": "Presets",
      "list_param": "preset",
      "count_param": "preset_count",
      "name_param": "preset_name",
      "items_param": "preset_list",
      "bank_name_param": "bank_name",
      "patch_param": "patch_in_bank",
      "hint": "Jog: browse  Click/Back: exit"
    },
    "osc1": {
      "label": "Osc 1",
      "params": [
        {"key":"osc1_level","label":"Level","type":"float","min":0.0,"max":1.0,"default":0.7071,"step":0.02},
        {"key":"osc1_frame","label":"Wave Frame","type":"int","min":0,"max":256,"default":0,"step":1},
        {"key":"osc1_shape","label":"Shape Type","type":"enum","options":["None","Sync","Formant","Quantize","Bend","Squeeze","Pulse","FM <- Osc","FM <- Osc","FM <- Sample","RM <- Osc","RM <- Osc","RM <- Sample"],"default":0},
        {"key":"osc1_shape_amt","label":"Shape Amt","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
        {"key":"osc1_transpose","label":"Transpose","type":"int","min":-48,"max":48,"default":0},
        {"key":"osc1_tune","label":"Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
        {"key":"osc1_unison","label":"Unison","type":"int","min":1,"max":16,"default":1},
        {"key":"osc1_detune","label":"Detune","type":"float","min":0.0,"max":10.0,"default":4.4721,"step":0.05}
      ],
      "knobs": ["osc1_level","osc1_frame","osc1_shape","osc1_shape_amt","osc1_transpose","osc1_tune","osc1_unison","osc1_detune"]
    },
    "osc2": {
      "label": "Osc 2",
      "params": [
        {"key":"osc2_level","label":"Level","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
        {"key":"osc2_frame","label":"Wave Frame","type":"int","min":0,"max":256,"default":0,"step":1},
        {"key":"osc2_shape","label":"Shape Type","type":"enum","options":["None","Sync","Formant","Quantize","Bend","Squeeze","Pulse","FM <- Osc","FM <- Osc","FM <- Sample","RM <- Osc","RM <- Osc","RM <- Sample"],"default":0},
        {"key":"osc2_shape_amt","label":"Shape Amt","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
        {"key":"osc2_transpose","label":"Transpose","type":"int","min":-48,"max":48,"default":0},
        {"key":"osc2_tune","label":"Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
        {"key":"osc2_unison","label":"Unison","type":"int","min":1,"max":16,"default":1},
        {"key":"osc2_detune","label":"Detune","type":"float","min":0.0,"max":10.0,"default":4.4721,"step":0.05}
      ],
      "knobs": ["osc2_level","osc2_frame","osc2_shape","osc2_shape_amt","osc2_transpose","osc2_tune","osc2_unison","osc2_detune"]
    },
    "mix": {
      "label": "Mix",
      "params": [
        {"key":"osc1_level","label":"Osc 1 Level","type":"float","min":0.0,"max":1.0,"default":0.7071,"step":0.02},
        {"key":"osc2_level","label":"Osc 2 Level","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
        {"key":"sub_level","label":"Sub Level","type":"float","min":0.0,"max":1.0,"default":0.0,"step":0.02},
        {"key":"voice_level","label":"Voice Level","type":"float","min":0.0,"max":1.0,"default":1.0,"step":0.02}
      ],
      "knobs": ["osc1_level","osc2_level","sub_level","voice_level"]
    },
    "voice": {
      "label": "Voice",
      "params": [
        {"key":"voice_level","label":"Voice Level","type":"float","min":0.0,"max":1.0,"default":1.0,"step":0.02},
        {"key":"transpose","label":"Transpose","type":"int","min":-48,"max":48,"default":0},
        {"key":"tune","label":"Tune","type":"float","min":-1.0,"max":1.0,"default":0.0,"step":0.01,"unit":"cents"},
        {"key":"polyphony","label":"Polyphony","type":"int","min":1,"max":32,"default":8},
        {"key":"portamento","label":"Portamento","type":"float","min":-10.0,"max":4.0,"default":-10.0,"step":0.1,"unit":"s"}
      ],
      "knobs": ["voice_level","transpose","tune","polyphony","portamento"]
    },
    "fx": {
      "label": "FX",
      "params": [
        {"key":"chorus_on","label":"Chorus On","type":"enum","options":["Off","On"],"default":0},
        {"key":"chorus_mix","label":"Chorus Mix","type":"float","min":0.0,"max":1.0,"default":0.5,"step":0.02},
        {"key":"reverb_on","label":"Reverb On","type":"enum","options":["Off","On"],"default":0},
        {"key":"reverb_mix","label":"Reverb Mix","type":"float","min":0.0,"max":1.0,"default":0.25,"step":0.02}
      ],
      "knobs": ["chorus_on","chorus_mix","reverb_on","reverb_mix"]
    }
  }
})json";

const ParamBinding* find_binding(const char* module_key) {
  for (const ParamBinding& binding : kBindings) {
    if (strcmp(binding.module_key, module_key) == 0)
      return &binding;
  }
  return nullptr;
}

class WaveWarpSynth : public HeadlessSynth {
public:
  explicit WaveWarpSynth(const char* module_dir) : seconds_time_(0.0) {
    engine_->setSampleRate(MOVE_SAMPLE_RATE);
    engine_->updateAllModulationSwitches();
    midi_manager_->setSampleRate(MOVE_SAMPLE_RATE);
    engine_->setBpm(read_bpm());
    render_buffer_ = std::make_unique<AudioSampleBuffer>(2, MOVE_FRAMES_PER_BLOCK);
    LoadSave::initSaveInfo(save_info_);
    discover_bundled_presets(module_dir);
    loadInitPreset();
    if (!load_bundled_preset(0))
      apply_startup_patch();
  }

  void enqueue_midi(const uint8_t* msg, int len) {
    if (msg == nullptr || len <= 0)
      return;

    const uint8_t status = msg[0] & 0xF0;
    if (status != 0x80 && status != 0x90 && status != 0xA0 &&
        status != 0xB0 && status != 0xC0 && status != 0xD0 &&
        status != 0xE0) {
      return;
    }

    const ScopedLock lock(midi_lock_);
    pending_midi_.addEvent(msg, len, 0);
  }

  bool set_bound_param(const char* module_key, const char* value) {
    if (strcmp(module_key, "cutoff") == 0 || strcmp(module_key, "resonance") == 0) {
      char* end = nullptr;
      const double parsed = strtod(value, &end);
      if (end == value)
        return false;
      const float final_value = static_cast<float>(parsed);
      if (strcmp(module_key, "cutoff") == 0) {
        set_control_if_present("filter_1_cutoff", final_value);
        set_control_if_present("filter_fx_cutoff", final_value);
      } else {
        set_control_if_present("filter_1_resonance", final_value);
        set_control_if_present("filter_fx_resonance", final_value);
      }
      return true;
    }

    const ParamBinding* binding = find_binding(module_key);
    if (binding == nullptr)
      return false;

    if (controls_.count(binding->vital_key) == 0)
      return false;

    char* end = nullptr;
    const double parsed = strtod(value, &end);
    if (end == value)
      return false;

    double final_value = parsed;
    if (binding->integer_value)
      final_value = std::nearbyint(parsed);

    controls_[binding->vital_key]->set(static_cast<vital::mono_float>(final_value));
    return true;
  }

  bool set_special_param(const char* module_key, const char* value) {
    if (strcmp(module_key, "preset") == 0) {
      char* end = nullptr;
      const long parsed = strtol(value, &end, 10);
      if (end == value)
        return false;
      return load_bundled_preset(static_cast<int>(parsed));
    }

    if (strcmp(module_key, "bank_index") == 0) {
      char* end = nullptr;
      const long parsed = strtol(value, &end, 10);
      if (end == value)
        return false;
      return parsed == 0;
    }

    return false;
  }

  bool get_bound_param(const char* module_key, char* buf, int buf_len) {
    if (strcmp(module_key, "cutoff") == 0) {
      const char* key = controls_.count("filter_1_cutoff") ? "filter_1_cutoff" : "filter_fx_cutoff";
      if (controls_.count(key) == 0)
        return false;
      snprintf(buf, buf_len, "%.6f", controls_[key]->value());
      return true;
    }

    if (strcmp(module_key, "resonance") == 0) {
      const char* key = controls_.count("filter_1_resonance") ? "filter_1_resonance" : "filter_fx_resonance";
      if (controls_.count(key) == 0)
        return false;
      snprintf(buf, buf_len, "%.6f", controls_[key]->value());
      return true;
    }

    const ParamBinding* binding = find_binding(module_key);
    if (binding == nullptr || controls_.count(binding->vital_key) == 0)
      return false;

    const float value = controls_[binding->vital_key]->value();
    if (binding->integer_value)
      snprintf(buf, buf_len, "%d", static_cast<int>(std::lround(value)));
    else
      snprintf(buf, buf_len, "%.6f", value);
    return true;
  }

  int get_special_param(const char* key, char* buf, int buf_len) {
    if (strcmp(key, "name") == 0 || strcmp(key, "preset_name") == 0) {
      if (!bundled_presets_.empty() && current_preset_index_ >= 0 &&
          current_preset_index_ < static_cast<int>(bundled_presets_.size())) {
        return snprintf(buf, buf_len, "%s", bundled_presets_[current_preset_index_].name.toRawUTF8());
      }
      return snprintf(buf, buf_len, "%s", get_current_preset_name().toRawUTF8());
    }

    if (strcmp(key, "preset_author") == 0) {
      if (!bundled_presets_.empty() && current_preset_index_ >= 0 &&
          current_preset_index_ < static_cast<int>(bundled_presets_.size()) &&
          !bundled_presets_[current_preset_index_].author.isEmpty()) {
        return snprintf(buf, buf_len, "%s", bundled_presets_[current_preset_index_].author.toRawUTF8());
      }
      return snprintf(buf, buf_len, "%s", get_current_preset_author().toRawUTF8());
    }

    if (strcmp(key, "preset_style") == 0) {
      if (!bundled_presets_.empty() && current_preset_index_ >= 0 &&
          current_preset_index_ < static_cast<int>(bundled_presets_.size()) &&
          !bundled_presets_[current_preset_index_].style.isEmpty()) {
        return snprintf(buf, buf_len, "%s", bundled_presets_[current_preset_index_].style.toRawUTF8());
      }
      return snprintf(buf, buf_len, "%s", get_current_preset_style().toRawUTF8());
    }

    if (strcmp(key, "preset") == 0)
      return snprintf(buf, buf_len, "%d", current_preset_index_);

    if (strcmp(key, "preset_count") == 0)
      return snprintf(buf, buf_len, "%d", static_cast<int>(bundled_presets_.size()));

    if (strcmp(key, "preset_list") == 0) {
      std::string json = "[";
      for (size_t i = 0; i < bundled_presets_.size(); ++i) {
        if (i > 0)
          json += ",";
        std::string name = bundled_presets_[i].name.toStdString();
        for (size_t pos = 0; (pos = name.find('"', pos)) != std::string::npos; pos += 1)
          name[pos] = '\'';
        json += "{\"index\":";
        json += std::to_string(i);
        json += ",\"name\":\"";
        json += name;
        json += "\"}";
      }
      json += "]";
      if (static_cast<int>(json.size()) >= buf_len)
        return -1;
      strcpy(buf, json.c_str());
      return static_cast<int>(json.size());
    }

    if (strcmp(key, "patch_in_bank") == 0)
      return snprintf(buf, buf_len, "%d", current_preset_index_ + 1);

    if (strcmp(key, "bank_count") == 0)
      return snprintf(buf, buf_len, "1");

    if (strcmp(key, "bank_index") == 0)
      return snprintf(buf, buf_len, "0");

    if (strcmp(key, "bank_name") == 0)
      return snprintf(buf, buf_len, "WaveWarp Free");

    return -1;
  }

  void render_block_to_int16(int16_t* out_interleaved_lr, int frames) {
    if (out_interleaved_lr == nullptr || frames <= 0)
      return;

    render_buffer_->clear();
    MidiBuffer midi_messages;
    {
      const ScopedLock lock(midi_lock_);
      midi_messages.swapWith(pending_midi_);
    }

    engine_->setBpm(read_bpm());
    engine_->correctToTime(seconds_time_);
    processMidi(midi_messages, 0, frames);
    processAudio(render_buffer_.get(), 2, frames, 0);

    const float* left = render_buffer_->getReadPointer(0);
    const float* right = render_buffer_->getReadPointer(1);
    for (int i = 0; i < frames; ++i) {
      const float l = clamp_sample(left[i]);
      const float r = clamp_sample(right[i]);
      out_interleaved_lr[i * 2] = static_cast<int16_t>(std::lround(l * 32767.0f));
      out_interleaved_lr[i * 2 + 1] = static_cast<int16_t>(std::lround(r * 32767.0f));
    }

    seconds_time_ += static_cast<double>(frames) / static_cast<double>(MOVE_SAMPLE_RATE);
  }

private:
  struct BundledPreset {
    File file;
    String name;
    String author;
    String style;
  };

  void set_control_if_present(const char* key, float value) {
    if (controls_.count(key))
      controls_[key]->set(value);
  }

  void apply_save_info_to_synth(const BundledPreset& preset) {
    const String preset_name = !save_info_["preset_name"].isEmpty() ? save_info_["preset_name"] : preset.name;
    const String author = !save_info_["author"].isEmpty() ? save_info_["author"] : preset.author;
    const String comments = save_info_["comments"];
    const String style = !save_info_["style"].isEmpty() ? save_info_["style"] : preset.style;

    setPresetName(preset_name);
    if (!author.isEmpty())
      setAuthor(author);
    if (!comments.isEmpty())
      setComments(comments);
    if (!style.isEmpty())
      setStyle(style);

    for (int i = 0; i < vital::kNumMacros; ++i) {
      const std::string key = "macro" + std::to_string(i + 1);
      auto found = save_info_.find(key);
      if (found != save_info_.end() && !found->second.isEmpty())
        setMacroName(i, found->second);
    }
  }

  void discover_bundled_presets(const char* module_dir) {
    bundled_presets_.clear();
    current_preset_index_ = 0;

    if (module_dir == nullptr || module_dir[0] == '\0')
      return;

    File presets_dir(String(module_dir) + "/presets");
    if (!presets_dir.isDirectory())
      return;

    Array<File> preset_files;
    presets_dir.findChildFiles(preset_files, File::findFiles, true, "*.vital");
    LoadSave::FileSorterAscending sorter;
    preset_files.sort(sorter);

    for (const File& preset_file : preset_files) {
      BundledPreset preset;
      preset.file = preset_file;
      preset.name = preset_file.getFileNameWithoutExtension();
      preset.author = LoadSave::getAuthorFromFile(preset_file);
      preset.style = LoadSave::getStyleFromFile(preset_file);
      bundled_presets_.push_back(std::move(preset));
    }
  }

  bool load_bundled_preset(int index) {
    if (bundled_presets_.empty() || index < 0 || index >= static_cast<int>(bundled_presets_.size()))
      return false;

    const BundledPreset& preset = bundled_presets_[index];
    const String preset_text = preset.file.loadFileAsString();
    if (preset_text.isEmpty())
      return false;

    json state = json::parse(preset_text.toStdString(), nullptr, false);
    if (state.is_discarded())
      return false;

    LoadSave::initSaveInfo(save_info_);
    if (!LoadSave::jsonToState(this, save_info_, state))
      return false;

    current_preset_index_ = index;
    apply_save_info_to_synth(preset);
    return true;
  }

  void apply_startup_patch() {
    /* Make the first page immediately useful on a plain init patch. */
    set_control_if_present("filter_1_on", 1.0f);
    set_control_if_present("filter_1_cutoff", 110.0f);
    set_control_if_present("filter_1_resonance", 0.15f);
    set_control_if_present("filter_1_mix", 1.0f);
    set_control_if_present("filter_1_osc1_input", 1.0f);
    set_control_if_present("filter_1_osc2_input", 1.0f);
    set_control_if_present("osc_1_on", 1.0f);
    set_control_if_present("osc_1_level", 0.8f);
    set_control_if_present("osc_2_on", 1.0f);
    set_control_if_present("osc_2_level", 0.0f);
    set_control_if_present("osc_2_transpose", 12.0f);
    set_control_if_present("sub_on", 1.0f);
    set_control_if_present("sub_level", 0.0f);
    set_control_if_present("reverb_on", 0.0f);
    set_control_if_present("chorus_on", 0.0f);
  }

  float read_bpm() const {
    if (g_host && g_host->get_bpm) {
      const float bpm = g_host->get_bpm();
      if (bpm > 0.0f)
        return bpm;
    }
    return 120.0f;
  }

  String get_current_preset_name() {
    const String preset_name = getPresetName();
    if (!preset_name.isEmpty())
      return preset_name;
    return "Init";
  }

  String get_current_preset_author() {
    const String author = getAuthor();
    if (!author.isEmpty())
      return author;
    return "WaveWarp";
  }

  String get_current_preset_style() {
    const String style = getStyle();
    if (!style.isEmpty())
      return style;
    return "Template";
  }

  CriticalSection midi_lock_;
  MidiBuffer pending_midi_;
  std::unique_ptr<AudioSampleBuffer> render_buffer_;
  std::map<std::string, String> save_info_;
  std::vector<BundledPreset> bundled_presets_;
  int current_preset_index_ = 0;
  double seconds_time_;
};

struct wavewarp_instance_t {
  std::unique_ptr<WaveWarpSynth> synth;
  std::string last_error;
};

void set_error(wavewarp_instance_t* instance, const char* message) {
  if (instance == nullptr)
    return;
  instance->last_error = message ? message : "";
  if (!instance->last_error.empty())
    log_message("%s", instance->last_error.c_str());
}

void* create_instance(const char* module_dir, const char* json_defaults) {
  (void)json_defaults;

  std::unique_ptr<wavewarp_instance_t> instance(new wavewarp_instance_t());
  try {
    instance->synth = std::make_unique<WaveWarpSynth>(module_dir);
    log_message("created instance");
  } catch (const std::exception& e) {
    set_error(instance.get(), e.what());
    return nullptr;
  } catch (...) {
    set_error(instance.get(), "failed to create synth instance");
    return nullptr;
  }
  return instance.release();
}

void destroy_instance(void* opaque) {
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  delete instance;
}

void on_midi(void* opaque, const uint8_t* msg, int len, int source) {
  (void)source;
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  if (instance == nullptr || instance->synth == nullptr)
    return;
  instance->synth->enqueue_midi(msg, len);
}

void set_param(void* opaque, const char* key, const char* val) {
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  if (instance == nullptr || instance->synth == nullptr || key == nullptr || val == nullptr)
    return;

  if (instance->synth->set_special_param(key, val)) {
    instance->last_error.clear();
    return;
  }

  if (instance->synth->set_bound_param(key, val)) {
    instance->last_error.clear();
    return;
  }

  char error[256];
  snprintf(error, sizeof(error), "unknown or invalid parameter: %s", key);
  set_error(instance, error);
}

int get_param(void* opaque, const char* key, char* buf, int buf_len) {
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  if (instance == nullptr || instance->synth == nullptr || key == nullptr || buf == nullptr || buf_len <= 0)
    return -1;

  if (strcmp(key, "chain_params") == 0) {
    const int len = static_cast<int>(strlen(kChainParamsJson));
    if (len >= buf_len)
      return -1;
    strcpy(buf, kChainParamsJson);
    return len;
  }

  if (strcmp(key, "ui_hierarchy") == 0) {
    const int len = static_cast<int>(strlen(kUiHierarchyJson));
    if (len >= buf_len)
      return -1;
    strcpy(buf, kUiHierarchyJson);
    return len;
  }

  const int special_len = instance->synth->get_special_param(key, buf, buf_len);
  if (special_len >= 0)
    return special_len;

  if (!instance->synth->get_bound_param(key, buf, buf_len))
    return -1;
  return static_cast<int>(strlen(buf));
}

int get_error(void* opaque, char* buf, int buf_len) {
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  if (instance == nullptr || buf == nullptr || buf_len <= 0)
    return 0;

  if (instance->last_error.empty())
    return 0;

  snprintf(buf, buf_len, "%s", instance->last_error.c_str());
  return static_cast<int>(strlen(buf));
}

void render_block(void* opaque, int16_t* out_interleaved_lr, int frames) {
  wavewarp_instance_t* instance = static_cast<wavewarp_instance_t*>(opaque);
  if (instance == nullptr || instance->synth == nullptr) {
    if (out_interleaved_lr != nullptr && frames > 0)
      memset(out_interleaved_lr, 0, sizeof(int16_t) * frames * 2);
    return;
  }

  try {
    instance->synth->render_block_to_int16(out_interleaved_lr, frames);
    instance->last_error.clear();
  } catch (const std::exception& e) {
    set_error(instance, e.what());
    memset(out_interleaved_lr, 0, sizeof(int16_t) * frames * 2);
  } catch (...) {
    set_error(instance, "render failed");
    memset(out_interleaved_lr, 0, sizeof(int16_t) * frames * 2);
  }
}

plugin_api_v2_t g_plugin_api = {
  MOVE_PLUGIN_API_VERSION_2,
  create_instance,
  destroy_instance,
  on_midi,
  set_param,
  get_param,
  get_error,
  render_block,
};

}  // namespace

extern "C" plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t* host) {
  g_host = host;
  log_message("initialised");
  return &g_plugin_api;
}
