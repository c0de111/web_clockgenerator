#include "webserver_pages.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "build_info.h"

static void html_escape(const char *src, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) {
        return;
    }
    size_t out_idx = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (*src && out_idx < dst_len - 1) {
        const char *replacement = NULL;
        switch (*src) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            default: break;
        }
        if (replacement) {
            size_t rep_len = strlen(replacement);
            if (out_idx + rep_len >= dst_len) {
                break;
            }
            memcpy(dst + out_idx, replacement, rep_len);
            out_idx += rep_len;
        } else {
            dst[out_idx++] = *src;
        }
        ++src;
    }
    dst[out_idx] = '\0';
}

void webserver_build_landing_page(char *buffer, size_t max_len, uint64_t frequency_hz,
                                  uint8_t drive_ma, bool output_enabled,
                                  const char *status_message, bool is_error,
                                  const char *morse_text, uint16_t morse_wpm, int16_t morse_fwpm,
                                  bool morse_playing, const char *morse_status,
                                  bool morse_hold_active) {
    if (!buffer || max_len == 0) {
        return;
    }

    const char *msg = (status_message && *status_message) ? status_message : NULL;
    const char *sel2 = (drive_ma == 2) ? " selected" : "";
    const char *sel4 = (drive_ma == 4) ? " selected" : "";
    const char *sel6 = (drive_ma == 6) ? " selected" : "";
    const char *sel8 = (drive_ma == 8) ? " selected" : "";
    const char *toggle_class = output_enabled ? "on" : "off";
    const char *toggle_aria = output_enabled ? "true" : "false";
    const char *toggle_text = output_enabled ? "Output ON" : "Output OFF";
    const char *morse_text_display = (morse_text && *morse_text) ? morse_text : "Hi!";
    const char *morse_status_text = (morse_status && *morse_status) ? morse_status : "Idle";
    const char *details_open = (morse_playing || morse_hold_active) ? " open" : "";
    const char *morse_status_class = morse_playing ? "playing" :
                                      (strcmp(morse_status_text, "Stopped") == 0 ? "stopped" : "idle");
    const char *play_disabled = morse_playing ? " disabled" : "";
    const char *stop_disabled = morse_playing ? "" : " disabled";
    const char *playing_attr = morse_playing ? "true" : "false";
    const char *hold_attr = morse_hold_active ? "true" : "false";
    const char *output_toggle_disabled = morse_hold_active ? " disabled" : "";

    char morse_text_html[32] = {0};
    html_escape(morse_text_display, morse_text_html, sizeof(morse_text_html));

    char morse_status_html[32] = {0};
    html_escape(morse_status_text, morse_status_html, sizeof(morse_status_html));

    if (morse_wpm < 1 || morse_wpm > 1000) {
        morse_wpm = 15;
    }

    char fwpm_value[8] = {0};
    if (morse_fwpm > 0) {
        snprintf(fwpm_value, sizeof(fwpm_value), "%d", morse_fwpm);
    } else {
        fwpm_value[0] = '\0';
    }

    char status_html[256] = {0};
    if (msg) {
        const char *status_class = is_error ? "status error" : "status ok";
        snprintf(status_html, sizeof(status_html),
                 "<div class=\"%s\"><span>%s</span></div>", status_class, msg);
    }

    const char *footer_text =
        "<span class=\"footer-line\">Configure the Si5351A output.</span>"
        "<span class=\"footer-line\">Frequency is applied to CLK0; drive strength maps to the chip's discrete 2/4/6/8 mA settings.</span>"
        "<span class=\"footer-meta\">Build " BUILD_GIT_COMMIT " &bull; " BUILD_COMPILED_AT "</span>";

    const char *default_status = "<div class=\"status ok\"><span>Clock generator ready</span></div>";

    snprintf(buffer, max_len,
             "<!DOCTYPE html>"
             "<html lang=\"en\">"
             "<head>"
             "<meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
             "<title>Clock Generator</title>"
             "<style>"
             "body{font-family:sans-serif;background:#f5f7fa;margin:0;}"
             ".page{display:flex;justify-content:center;align-items:flex-start;padding:2em;}"
             ".card{background:#fff;border-radius:12px;box-shadow:0 8px 24px rgba(15,23,42,0.15);padding:2em;max-width:460px;width:100%;}"
             ".card h1{text-align:center;margin:0;color:#1f2937;}"
             ".card form{display:flex;flex-direction:column;gap:1.1em;margin-top:1.2em;}"
             ".card label{display:flex;flex-direction:column;font-weight:600;color:#374151;gap:0.45em;}"
             ".card input,.card select{font-size:1em;padding:0.55em 0.7em;border:1px solid #d1d5db;border-radius:8px;box-shadow:inset 0 1px 2px rgba(0,0,0,0.05);}"
             ".adjust-row{display:flex;gap:0.6em;align-items:center;flex-wrap:wrap;}"
             "#frequency-spinner{flex:1 1 260px;min-width:160px;}"
             ".output-toggle{flex:0 0 auto;padding:0.55em 0.9em;border:none;border-radius:8px;font-weight:600;cursor:pointer;transition:background 0.15s ease,color 0.15s ease;}"
             ".output-toggle.on{background:#10b981;color:#064e3b;}"
             ".output-toggle.off{background:#f87171;color:#7f1d1d;}"
             ".output-toggle:focus{outline:2px solid rgba(59,130,246,0.6);outline-offset:2px;}"
             ".output-toggle:disabled{opacity:0.6;cursor:not-allowed;}"
             ".morse-details{margin-top:1.8em;border:1px solid #e5e7eb;border-radius:12px;padding:1.1em 1.2em;background:#f9fafb;transition:box-shadow 0.2s ease,background 0.2s ease;}"
             ".morse-details[open]{background:#fff;box-shadow:0 10px 24px rgba(15,23,42,0.12);}"
             ".morse-details summary{font-weight:700;font-size:1.05em;color:#1f2937;cursor:pointer;outline:none;}"
             ".morse-panel{margin-top:1em;display:flex;flex-direction:column;gap:1em;}"
             ".morse-form{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:0.8em;}"
             ".morse-range{display:flex;gap:0.6em;align-items:flex-start;}"
             ".morse-range label{flex:1 1 0;}"
             ".morse-form label{display:flex;flex-direction:column;font-weight:600;color:#374151;gap:0.35em;}"
             ".morse-form input{font-size:1em;padding:0.55em 0.7em;border:1px solid #d1d5db;border-radius:8px;box-shadow:inset 0 1px 2px rgba(0,0,0,0.05);}"
             ".morse-actions{display:flex;gap:0.7em;flex-wrap:wrap;}"
             ".morse-stop-form{margin:0;}"
             ".morse-play,.morse-stop{padding:0.6em 1.1em;border:none;border-radius:8px;font-weight:600;cursor:pointer;transition:background 0.15s ease,color 0.15s ease,opacity 0.15s ease;}"
             ".morse-play{background:#2563eb;color:#f9fafb;}"
             ".morse-stop{background:#ef4444;color:#fff;}"
             ".morse-play:disabled{opacity:0.6;cursor:not-allowed;}"
             ".morse-stop:disabled{opacity:0.5;cursor:not-allowed;}"
             ".morse-status{font-weight:600;}"
             ".morse-status.playing span{color:#047857;}"
             ".morse-status.stopped span{color:#92400e;}"
             ".morse-status.idle span{color:#374151;}"
             ".digital{font-family:'DS-Digital','Segment7Standard','Courier New',monospace;letter-spacing:0.05em;background:#111827;color:#f9fafb;border-color:#1f2937;text-align:center;}"
             ".readout{display:flex;justify-content:center;align-items:center;font-size:1.2em;padding:0.75em;border:1px solid #1f2937;border-radius:8px;background:#111827;color:#f9fafb;box-shadow:inset 0 1px 3px rgba(0,0,0,0.25);}"
             ".step-group{display:flex;flex-wrap:wrap;gap:0.6em;}"
             ".step-option{display:flex;align-items:center;gap:0.35em;font-weight:500;font-size:0.95em;}"
             ".step-option input{width:auto;margin:0;}"
             ".status{margin-top:1em;padding:0.75em;border-radius:10px;border:1px solid #d1d5db;font-weight:600;text-align:center;}"
             ".status.ok{background:#e8f8ef;color:#1a6a2b;border-color:#9dd9a8;}"
             ".status.error{background:#fbeaea;color:#a32121;border-color:#f0a0a0;}"
             ".footer{text-align:center;margin-top:1.5em;font-size:0.9em;color:#4b5563;}"
             ".footer-line{display:block;}"
             ".footer-meta{display:block;margin-top:0.35em;font-size:0.75em;color:#6b7280;}"
             "@media (max-width:600px){.page{padding:1em;}.card{padding:1.5em;}}"
             "</style>"
             "<script>"
             "let submitTimer=null;"
             "function scheduleSubmit(){"
             "  if(submitTimer) clearTimeout(submitTimer);"
             "  submitTimer=setTimeout(function(){"
             "    const form=document.getElementById('signal-form');"
             "    if(form) form.requestSubmit();"
             "  },150);"
             "}"
             "window.addEventListener('DOMContentLoaded',function(){"
             "  const spinner=document.getElementById('frequency-spinner');"
             "  let suppressSubmit=false;"
             "  let manualEdit=false;"
             "  const display=document.getElementById('frequency-display');"
             "  const formatWithSeparators=function(value){"
             "    if(value===undefined||value===null) return '';"
             "    const digits=String(value).replace(/[^0-9]/g,'');"
             "    if(!digits.length) return '';"
             "    return digits.replace(/\\B(?=(\\d{3})+(?!\\d))/g,'.');"
             "  };"
             "  const syncDisplay=function(){"
             "    if(display&&spinner){"
             "      display.textContent=formatWithSeparators(spinner.value);"
             "    }"
             "  };"
             "  const updateStep=function(stepValue){"
             "    if(!spinner) return;"
             "    const numeric=parseInt(stepValue,10);"
             "    if(numeric>=1){"
             "      spinner.step=numeric;"
             "    }"
             "  };"
             "  if(spinner){"
             "    spinner.addEventListener('focus',function(){"
             "      manualEdit=false;"
             "      if(!suppressSubmit){"
             "        syncDisplay();"
             "      }"
             "    });"
             "    spinner.addEventListener('pointerdown',function(){"
             "      manualEdit=false;"
             "      suppressSubmit=false;"
             "    });"
             "    spinner.addEventListener('keydown',function(event){"
             "      if(event.key==='Enter'){"
             "        event.preventDefault();"
             "        manualEdit=false;"
             "        suppressSubmit=false;"
             "        syncDisplay();"
             "        scheduleSubmit();"
             "        return;"
             "      }"
             "      const manualKeys=['Backspace','Delete'];"
             "      const isDigit=event.key.length===1 && event.key>='0' && event.key<='9';"
             "      if(isDigit || manualKeys.indexOf(event.key)!==-1){"
             "        manualEdit=true;"
             "        suppressSubmit=true;"
             "      }"
             "    });"
             "    spinner.addEventListener('input',function(){"
             "      syncDisplay();"
             "      if(!suppressSubmit){"
             "        scheduleSubmit();"
             "      }"
             "    });"
             "    spinner.addEventListener('change',function(){"
             "      syncDisplay();"
             "      suppressSubmit=false;"
             "      manualEdit=false;"
             "      scheduleSubmit();"
             "    });"
             "    spinner.addEventListener('blur',function(){"
             "      if(manualEdit){"
             "        manualEdit=false;"
             "        suppressSubmit=false;"
             "        syncDisplay();"
             "        scheduleSubmit();"
             "      }"
             "    });"
             "    spinner.addEventListener('wheel',function(event){event.preventDefault();},{passive:false});"
             "    syncDisplay();"
             "  }"
             "  const stepRadios=document.querySelectorAll('input[name=\"step\"]');"
             "  if(stepRadios.length){"
             "    const savedStep=window.localStorage?localStorage.getItem('clockgen-step'):null;"
             "    let selectedValue=null;"
             "    stepRadios.forEach(function(radio){"
             "      if(savedStep && radio.value===savedStep){"
             "        radio.checked=true;"
             "        selectedValue=radio.value;"
             "      } else if(radio.checked && !selectedValue){"
             "        selectedValue=radio.value;"
             "      }"
             "    });"
             "    if(selectedValue){"
             "      updateStep(selectedValue);"
             "    } else if(spinner){"
             "      updateStep(spinner.step || '1000');"
             "    }"
             "    stepRadios.forEach(function(radio){"
             "      radio.addEventListener('change',function(){"
             "        if(radio.checked){"
             "          updateStep(radio.value);"
             "          if(window.localStorage){"
             "            localStorage.setItem('clockgen-step', radio.value);"
             "          }"
             "        }"
             "      });"
             "    });"
             "  }"
             "  const morseStatus=document.getElementById('morse-status');"
             "  const morseStatusText=document.getElementById('morse-status-text');"
             "  const morsePlay=document.getElementById('morse-play');"
             "  const morseStop=document.getElementById('morse-stop');"
             "  const morseDetails=document.getElementById('morse-details');"
             "  const outputToggle=document.getElementById('output-toggle');"
             "  if(outputToggle && morseStatus && morseStatus.getAttribute('data-hold')==='true'){outputToggle.disabled=true;}"
             "  if(morseDetails && typeof fetch==='function'){"
             "    morseDetails.addEventListener('toggle',function(){"
             "      const open=morseDetails.open;"
             "      if(outputToggle){outputToggle.disabled=open;}"
             "      const body='active='+(open?'1':'0');"
             "      fetch('/morse/hold',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body}).catch(function(){});"
             "    });"
             "  }"
             "  if(morseStatus && morseStatusText && typeof fetch==='function'){"
             "    const applyMorseStatus=function(data){"
             "      const statusText=(data && typeof data.status==='string')?data.status:'Idle';"
             "      const playing=!!(data && data.playing);"
             "      const holdActive=!!(data && data.hold);"
             "      morseStatusText.textContent=statusText;"
             "      morseStatus.classList.remove('playing','stopped','idle');"
             "      const className=playing?'playing':(statusText==='Stopped'?'stopped':'idle');"
             "      morseStatus.classList.add(className);"
             "      morseStatus.setAttribute('data-playing', playing?'true':'false');"
             "      morseStatus.setAttribute('data-hold', holdActive?'true':'false');"
             "      if(morsePlay){morsePlay.disabled=playing;}"
             "      if(morseStop){morseStop.disabled=!playing;}"
             "      if(outputToggle){outputToggle.disabled=holdActive;}"
             "      if(morseDetails && (playing || holdActive) && !morseDetails.open){morseDetails.open=true;}"
             "      if(morseDetails && morseDetails.open){try{morseDetails.scrollIntoView({behavior:'auto',block:'start'});}catch(e){}}"
             "    };"
             "    applyMorseStatus({playing:morseStatus.getAttribute('data-playing')==='true',status:morseStatusText.textContent,hold:morseStatus.getAttribute('data-hold')==='true'});"
             "    const pollMorse=function(){"
             "      fetch('/morse/status',{cache:'no-store'}).then(function(resp){"
             "        if(!resp.ok) throw new Error('status');"
             "        return resp.json();"
             "      }).then(function(data){applyMorseStatus(data);}).catch(function(){});"
             "    };"
             "    pollMorse();"
             "    setInterval(pollMorse,1000);"
             "  }"
             "});"
             "</script>"
             "</head>"
             "<body>"
             "<div class=\"page\"><div class=\"card\">"
             "<h1>Clock Generator</h1>"
             "%s"
             "<form id=\"signal-form\" method=\"POST\" action=\"/signal\">"
             "<label>Frequency (Hz)"
             "<div id=\"frequency-display\" class=\"readout digital\" role=\"status\" aria-live=\"polite\">%llu</div>"
             "</label>"
             "<label>Adjust"
             "<div class=\"adjust-row\">"
             "<input type=\"number\" name=\"frequency\" id=\"frequency-spinner\" class=\"digital\" min=\"8000\" max=\"200000000\" step=\"1000\" value=\"%llu\">"
             "<button type=\"submit\" name=\"action\" value=\"toggle-output\" id=\"output-toggle\" class=\"output-toggle %s\" aria-pressed=\"%s\"%s>%s</button>"
             "</div>"
             "</label>"
             "<label>Increment"
             "<div class=\"step-group\">"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"1\">1 Hz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"10\">10 Hz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"100\">100 Hz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"1000\" checked>1 kHz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"10000\">10 kHz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"100000\">100 kHz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"1000000\">1 MHz</label>"
             "<label class=\"step-option\"><input type=\"radio\" name=\"step\" value=\"10000000\">10 MHz</label>"
             "</div>"
             "</label>"
             "<label>Drive strength"
             "<select name=\"drive\" onchange=\"scheduleSubmit()\">"
             "<option value=\"2\"%s>2 mA</option>"
             "<option value=\"4\"%s>4 mA</option>"
             "<option value=\"6\"%s>6 mA</option>"
             "<option value=\"8\"%s>8 mA</option>"
             "</select>"
             "</label>"
             "</form>"
             "<details class=\"morse-details\"%s id=\"morse-details\">"
             "<summary>Morse Playback</summary>"
             "<div class=\"morse-panel\">"
             "<div id=\"morse-status\" class=\"morse-status %s\" data-playing=\"%s\" data-hold=\"%s\">Status: <span id=\"morse-status-text\">%s</span></div>"
             "<form class=\"morse-form\" method=\"POST\" action=\"/morse\">"
             "<label>Text"
             "<input type=\"text\" name=\"text\" maxlength=\"20\" value=\"%s\" required>"
             "</label>"
             "<div class=\"morse-range\">"
             "<label>WPM"
             "<input type=\"number\" name=\"wpm\" min=\"1\" max=\"1000\" value=\"%u\" required>"
             "</label>"
             "<label>Farnsworth WPM"
             "<input type=\"number\" name=\"fwpm\" min=\"1\" max=\"1000\" value=\"%s\" placeholder=\"optional\">"
             "</label>"
             "</div>"
             "<div class=\"morse-actions\">"
             "<button type=\"submit\" class=\"morse-play\" id=\"morse-play\"%s>Play</button>"
             "</div>"
             "</form>"
             "<form method=\"POST\" action=\"/morse/stop\" class=\"morse-stop-form\">"
             "<button type=\"submit\" class=\"morse-stop\" id=\"morse-stop\"%s>Stop</button>"
             "</form>"
             "</div>"
             "</details>"
             "<div class=\"footer\">%s</div>"
             "</div></div>"
             "</body>"
             "</html>",
             status_html[0] ? status_html : default_status,
             (unsigned long long)frequency_hz,
             (unsigned long long)frequency_hz,
             toggle_class,
             toggle_aria,
             output_toggle_disabled,
             toggle_text,
             sel2, sel4, sel6, sel8,
             details_open,
             morse_status_class,
             playing_attr,
             hold_attr,
             morse_status_html,
             morse_text_html,
             (unsigned)morse_wpm,
             fwpm_value,
             play_disabled,
             stop_disabled,
             footer_text);
}
