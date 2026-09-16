#include "app_common.h"

static const char s_index_html[] =
// One self-contained page keeps the AP useful even with no internet connection
// and no filesystem dependency beyond the SD data logs.
"<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Muon Readout</title><style>"
":root{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#182026;background:#f7f5ef}"
"body{margin:0}.bar{display:flex;gap:12px;align-items:center;justify-content:space-between;padding:14px 18px;background:#182026;color:white}"
"main{padding:16px;max-width:1180px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:12px}"
"section{background:white;border:1px solid #d9d5ca;border-radius:8px;padding:14px;margin-top:12px}section:first-child{margin-top:0}h1{font-size:20px;margin:0}h2{font-size:16px;margin:0 0 10px}"
"table{border-collapse:collapse;width:100%;font-size:13px}td,th{border-bottom:1px solid #ece7dc;padding:6px;text-align:right}td:first-child,th:first-child{text-align:left}"
"button,input{font:inherit;border:1px solid #aaa;border-radius:6px;padding:8px;background:white}button{cursor:pointer;background:#1f6feb;color:white;border-color:#1f6feb}"
"button.secondary{background:white;color:#182026}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}"
".topStatus{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-bottom:12px}.badge{border:1px solid #d9d5ca;border-radius:8px;padding:12px;background:white}.badgeLabel{font-size:12px;color:#5f675a}.badgeValue{font-size:24px;font-weight:750;margin-top:4px}.badge.okBg{border-color:#8ac7a2;background:#f3fff7}.badge.badBg{border-color:#e39b95;background:#fff5f3}"
".ok{color:#18794e}.bad{color:#c0342b}.hero{border-color:#aabf98;background:#fbfff7}.countCards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}.countCard{border:1px solid #cddfc0;border-radius:8px;padding:12px;background:white}.countName{font-size:13px;color:#5b6754}.countValue{font-size:36px;font-weight:750;line-height:1.1;margin:6px 0}.countMeta{font-size:12px;color:#5f675a}.filesSection{margin-top:18px}@media(max-width:540px){.bar{display:block}.grid{grid-template-columns:1fr}.countValue{font-size:30px}}"
"</style></head><body><div class=\"bar\"><h1>gLOWCOST Muon Readout</h1><div class=\"mono\" id=\"clock\">--</div></div><main><div class=\"topStatus\"><div id=\"sdBadge\" class=\"badge\"><div class=\"badgeLabel\">SD Card</div><div class=\"badgeValue\">--</div><div class=\"mono\" id=\"sdBadgeFile\">--</div></div></div>"
"<div class=\"grid\"><section><h2>Status</h2><div id=\"status\">Loading...</div></section>"
"<section><h2>Time</h2><div class=\"row\"><button onclick=\"syncTime()\">Sync Browser Time</button></div>"
"<div class=\"row\"><input id=\"manualTime\" type=\"datetime-local\"><button class=\"secondary\" onclick=\"setManualTime()\">Set</button></div></section>"
"<section><h2>SD Log</h2><div id=\"sdlog\">--</div><div class=\"row\"><input id=\"runLabel\" maxlength=\"32\" placeholder=\"flight/location\"><button onclick=\"setRunLabel()\">Set Label</button></div><div class=\"row\"><a href=\"/api/sd.csv\">Download Current File</a></div></section>"
"<section><h2>Environment</h2><div id=\"envlog\">--</div><div class=\"row\"><a href=\"/api/env.csv\">Download Environment File</a></div></section>"
"<section><h2>High Voltage</h2><p>Startup sequence: HV off, FPGA flash, DAC init, then profile HV after settle.</p><div class=\"row\"><input id=\"hvByte\" class=\"mono\" value=\"ea\" maxlength=\"2\" size=\"4\"><button onclick=\"setHv()\">Set HV Byte</button><button class=\"secondary\" onclick=\"hvOff()\">Off</button></div></section>"
"<section><h2>FPGA / DAC</h2><div class=\"row\"><button onclick=\"flashFpga()\">HV Off + Flash FPGA + HV On</button><button class=\"secondary\" onclick=\"dacZero()\">Set Startup DAC</button></div>"
"<div class=\"row\"><input id=\"dacCh\" type=\"number\" min=\"0\" max=\"7\" value=\"0\"><input id=\"dacVal\" class=\"mono\" value=\"0000\" maxlength=\"4\" size=\"6\"><button class=\"secondary\" onclick=\"setDac()\">Set DAC</button></div></section></div>"
"<section><h2>Power</h2><p>For quiet data, sync/download/check briefly, then turn Wi-Fi off. HV cycles off during Wi-Fi shutdown, then counting resumes after settle.</p><div class=\"row\"><button onclick=\"keepWifiOn()\">Keep Wi-Fi On</button><button class=\"secondary\" onclick=\"allowWifiAutoOff()\">Allow Auto-Off</button><button onclick=\"powerSave()\">Turn Wi-Fi Off</button></div></section>"
"<section class=\"hero\"><h2>Live Minute Coincident Counts</h2><div id=\"countCards\" class=\"countCards\"></div></section>"
"<section><h2>Counts Detail</h2><table id=\"counts\"></table></section>"
"<section><h2>Minute Log</h2><div class=\"row\"><a href=\"/api/log.csv\">Download CSV</a></div><table id=\"log\"></table></section>"
"<section class=\"filesSection\"><h2>Previous SD Files</h2><div class=\"row\"><button class=\"secondary\" onclick=\"loadSdFiles()\">Refresh Files</button></div><div id=\"sdFiles\" class=\"mono\">--</div></section>"
"</main><script>"
"async function api(u){let r=await fetch(u); if(!r.ok) throw new Error(await r.text()); return r}"
"function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}"
"function fmtEpoch(e){return e>1600000000?new Date(e*1000).toLocaleString():'not set'}"
"async function refresh(){try{let j=await (await api('/api/status')).json(); document.getElementById('clock').textContent=fmtEpoch(j.epoch)+' / '+Math.round(j.uptime_ms/1000)+'s';"
"let sdBadge=document.getElementById('sdBadge'); sdBadge.className='badge '+(j.sd_mounted?'okBg':'badBg'); sdBadge.querySelector('.badgeValue').innerHTML=j.sd_mounted?'<span class=\"ok\">Ready</span>':'<span class=\"bad\">Not Ready</span>'; document.getElementById('sdBadgeFile').textContent=j.sd_mounted?(j.log_file||'mounted, no file'):'check card / slot';"
"document.getElementById('status').innerHTML='<p>AP <span class=\"mono\">'+j.ssid+'</span> at <span class=\"mono\">http://192.168.4.1</span></p><p>Power saving: '+(j.power_save_mode?'on':'off')+'</p><p>Wi-Fi auto-off: '+(j.wifi_keep_on?'disabled':'enabled')+'</p><p>FPGA DONE: <b class=\"'+(j.fpga_done?'ok':'bad')+'\">'+j.fpga_done+'</b></p><p>HV byte: <span class=\"mono\">0x'+j.hv_byte+'</span></p><p>Counting: '+(j.counting_enabled?'enabled':'settling/off')+'</p><p>HV settle remaining: '+Math.max(0,Math.ceil(j.hv_settle_remaining_ms/1000))+'s</p><p>Time: '+(j.time_set?'set':'not set')+'</p>';"
"document.getElementById('sdlog').innerHTML='<p>SD: '+(j.sd_mounted?'mounted':'not mounted')+'</p><p>File: <span class=\"mono\">'+esc(j.log_file||'none')+'</span></p><p>Label: <span class=\"mono\">'+esc(j.run_label||'')+'</span></p>';"
"document.getElementById('envlog').innerHTML='<p>BME280: '+(j.bme280_ok?'ready':'not found')+'</p><p>File: <span class=\"mono\">'+esc(j.env_log_file||'none')+'</span></p>'+((j.env_latest&&j.env_latest.valid)?'<p>Latest: '+j.env_latest.temp_c.toFixed(2)+' C, '+j.env_latest.pressure_hpa.toFixed(2)+' hPa, '+j.env_latest.humidity_pct.toFixed(2)+' %RH</p>':'<p>Latest: waiting</p>');"
"let cards=''; j.channels.forEach(c=>cards+='<div class=\"countCard\"><div class=\"countName\">'+esc(c.name)+'</div><div class=\"countValue\">'+c.last+'</div><div class=\"countMeta\">current '+c.current+' / total '+c.total+'</div></div>'); document.getElementById('countCards').innerHTML=cards;"
"let h='<tr><th>channel</th><th>current</th><th>last minute</th><th>total</th></tr>'; j.channels.forEach(c=>h+='<tr><td>'+esc(c.name)+'</td><td>'+c.current+'</td><td>'+c.last+'</td><td>'+c.total+'</td></tr>'); document.getElementById('counts').innerHTML=h;"
"let l='<tr><th>time</th>'; j.channels.forEach(c=>l+='<th>'+esc(c.name)+'</th>'); l+='</tr>'; j.records.forEach(r=>{l+='<tr><td>'+fmtEpoch(r.epoch)+'</td>'; r.counts.forEach(v=>l+='<td>'+v+'</td>'); l+='</tr>'}); document.getElementById('log').innerHTML=l;"
"}catch(e){document.getElementById('status').textContent=e.message}}"
"async function syncTime(){await api('/api/time?epoch='+Math.floor(Date.now()/1000)); refresh()}"
"async function autoSyncTime(){let e=Math.floor(Date.now()/1000); if(e>1600000000){try{await api('/api/time?epoch='+e)}catch(err){}} refresh()}"
"async function setManualTime(){let v=document.getElementById('manualTime').value; if(v) {await api('/api/time?epoch='+Math.floor(new Date(v).getTime()/1000)); refresh()}}"
"async function setRunLabel(){await api('/api/run_label?label='+encodeURIComponent(document.getElementById('runLabel').value)); refresh()}"
"async function loadSdFiles(){try{let j=await (await api('/api/sd_files')).json(); let h='<table><tr><th>file</th><th>modified</th><th>bytes</th><th></th></tr>'; j.files.forEach(f=>{h+='<tr><td>'+esc(f.name)+(f.current?' *':'')+'</td><td>'+esc(f.modified||'--')+'</td><td>'+f.size+'</td><td><a href=\"/api/sd_file?name='+encodeURIComponent(f.name)+'\">download</a></td></tr>'}); h+='</table>'; document.getElementById('sdFiles').innerHTML=h}catch(e){document.getElementById('sdFiles').textContent=e.message}}"
"async function setHv(){await api('/api/hv?byte='+encodeURIComponent(document.getElementById('hvByte').value)); refresh()}"
"async function hvOff(){await api('/api/hv?off=1'); refresh()}"
"async function flashFpga(){if(confirm('This will turn HV off, flash the FPGA, reload startup DAC values, then turn HV back on after the normal settle.')){await api('/api/fpga'); refresh()}}"
"async function dacZero(){for(let i=0;i<4;i++) await api('/api/dac?ch='+i+'&value=02f1'); for(let i=4;i<8;i++) await api('/api/dac?ch='+i+'&value=0070'); refresh()}"
"async function setDac(){await api('/api/dac?ch='+document.getElementById('dacCh').value+'&value='+document.getElementById('dacVal').value); refresh()}"
"async function keepWifiOn(){await api('/api/wifi_keep_on?enable=1'); refresh()}"
"async function allowWifiAutoOff(){await api('/api/wifi_keep_on?enable=0'); refresh()}"
"async function powerSave(){if(confirm('Turn Wi-Fi off until next reboot? HV will cycle off briefly and counting resumes after settle.')){try{await syncTime()}catch(e){} await api('/api/power_save'); document.body.innerHTML='<main><section><h2>Power saving mode enabled</h2><p>Wi-Fi is shutting down. HV will restart after 3 seconds, then counting resumes after settle.</p></section></main>'}}"
"autoSyncTime(); loadSdFiles(); setInterval(refresh,2000);</script></body></html>";


// Extract one query parameter from an HTTP request.
static bool query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

// Send a plain-text response for the small command-style API endpoints.
static esp_err_t text_response(httpd_req_t *req, const char *text)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

// Serve the embedded detector dashboard.
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_index_html, HTTPD_RESP_USE_STRLEN);
}

// Append one RAM log record into an existing JSON response buffer.
static void json_record_append(char *buf, size_t buf_len, size_t *offset, const count_record_t *record)
{
    int written = snprintf(buf + *offset, buf_len - *offset,
                           "{\"epoch\":%lld,\"uptime_ms\":%" PRId64 ",\"counts\":[",
                           (long long)record->epoch, record->uptime_ms);
    if (written > 0) {
        *offset += (size_t)written;
    }
    for (size_t i = 0; i < COUNT_CHANNELS && *offset < buf_len; i++) {
        written = snprintf(buf + *offset, buf_len - *offset, "%s%" PRIu32, i ? "," : "", record->counts[i]);
        if (written > 0) {
            *offset += (size_t)written;
        }
    }
    if (*offset < buf_len) {
        written = snprintf(buf + *offset, buf_len - *offset, "]}");
        if (written > 0) {
            *offset += (size_t)written;
        }
    }
}

// Format one detector record as the Pi-style CSV row used by downloads and
// quick display clients.
static void csv_record_line(const count_record_t *record, char *line, size_t line_len)
{
    char iso[32];
    csv_time_string(record->epoch, iso, sizeof(iso));
    size_t off = snprintf(line, line_len, "%lld,%s", (long long)record->epoch, iso);
    for (size_t i = 0; i < COUNT_CHANNELS && off < line_len; i++) {
        int written = snprintf(line + off, line_len - off, ",%" PRIu32, record->counts[i]);
        if (written > 0) {
            off += (size_t)written;
        }
    }
    if (off + 1 < line_len) {
        snprintf(line + off, line_len - off, "\n");
    }
}

// Return the live detector status used by the dashboard: counts, HV, SD, time,
// FPGA, and environment state.
static esp_err_t status_handler(httpd_req_t *req)
{
    uint32_t live[COUNT_CHANNELS];
    uint32_t last[COUNT_CHANNELS];
    uint64_t totals[COUNT_CHANNELS];
    count_record_t records[8];
    size_t record_count;
    uint8_t hv_byte;
    bool time_set;
    bool fpga_ok;
    bool counting_enabled;
    bool power_save_mode;
    bool wifi_keep_on;
    int64_t settle_remaining_ms;
    bool sd_mounted = false;
    char log_path[sizeof(s_log_path)] = "";
    char env_log_path[sizeof(s_env_log_path)] = "";
    char run_label[sizeof(s_run_label)] = "";
    bool bme280_ok;
    bme280_reading_t env_latest;

    // Take fast snapshots under the small locks, then format JSON after the
    // locks are released. The web task should not block counting or SD writes.
    snapshot_counts(live, false);
    portENTER_CRITICAL(&s_state_mux);
    memcpy(s_live_counts, live, sizeof(s_live_counts));
    memcpy(last, s_last_counts, sizeof(last));
    memcpy(totals, s_totals, sizeof(totals));
    hv_byte = s_hv_byte;
    time_set = s_time_set;
    fpga_ok = s_fpga_ok;
    counting_enabled = s_counting_enabled;
    power_save_mode = s_power_save_mode;
    wifi_keep_on = s_wifi_keep_on;
    bme280_ok = s_bme280_ok;
    env_latest = s_bme280_latest;
    settle_remaining_ms = s_hv_settle_until_ms ? s_hv_settle_until_ms - (esp_timer_get_time() / 1000) : 0;
    record_count = s_log_count < 8 ? s_log_count : 8;
    for (size_t i = 0; i < record_count; i++) {
        size_t newest = (s_log_head + LOG_RECORDS - 1 - i) % LOG_RECORDS;
        records[i] = s_log[newest];
    }
    portEXIT_CRITICAL(&s_state_mux);
    if (s_sd_mutex) {
        xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
        sd_mounted = s_sd_mounted;
        snprintf(log_path, sizeof(log_path), "%s", s_log_path);
        snprintf(env_log_path, sizeof(env_log_path), "%s", s_env_log_path);
        snprintf(run_label, sizeof(run_label), "%s", s_run_label);
        xSemaphoreGive(s_sd_mutex);
    }

    char buf[4096];
    size_t off = 0;
    off += snprintf(buf + off, sizeof(buf) - off,
                    "{\"ssid\":\"%s\",\"uptime_ms\":%" PRId64 ",\"epoch\":%lld,\"time_set\":%s,\"power_save_mode\":%s,\"wifi_keep_on\":%s,\"fpga_done\":%s,\"fpga_ok\":%s,\"hv_byte\":\"%02x\",\"counting_enabled\":%s,\"hv_settle_remaining_ms\":%" PRId64 ",\"sd_mounted\":%s,\"log_file\":\"%s\",\"env_log_file\":\"%s\",\"run_label\":\"%s\",\"bme280_ok\":%s,\"env_latest\":{\"valid\":%s,\"temp_c\":%.3f,\"pressure_hpa\":%.3f,\"humidity_pct\":%.3f},\"channels\":[",
                    WIFI_AP_SSID, esp_timer_get_time() / 1000, (long long)time(NULL),
                    time_set ? "true" : "false",
                    power_save_mode ? "true" : "false",
                    wifi_keep_on ? "true" : "false",
                    gpio_get_level(PIN_FPGA_DONE) ? "true" : "false",
                    fpga_ok ? "true" : "false",
                    hv_byte,
                    counting_enabled ? "true" : "false",
                    settle_remaining_ms > 0 ? settle_remaining_ms : 0,
                    sd_mounted ? "true" : "false",
                    log_path,
                    env_log_path,
                    run_label,
                    bme280_ok ? "true" : "false",
                    env_latest.valid ? "true" : "false",
                    env_latest.temp_c,
                    env_latest.pressure_hpa,
                    env_latest.humidity_pct);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        off += snprintf(buf + off, sizeof(buf) - off,
                        "%s{\"name\":\"%s\",\"current\":%" PRIu32 ",\"last\":%" PRIu32 ",\"total\":%" PRIu64 "}",
                        i ? "," : "", s_count_names[i], live[i], last[i], totals[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "],\"records\":[");
    for (size_t i = 0; i < record_count; i++) {
        if (i) {
            off += snprintf(buf + off, sizeof(buf) - off, ",");
        }
        json_record_append(buf, sizeof(buf), &off, &records[i]);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, off);
}

// Download the in-RAM recent minute log as CSV.
static esp_err_t log_csv_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=muon_log.csv");
    char header[192];
    count_csv_header(header, sizeof(header));
    httpd_resp_sendstr_chunk(req, header);

    count_record_t records[LOG_RECORDS];
    size_t count;
    portENTER_CRITICAL(&s_state_mux);
    count = s_log_count;
    for (size_t i = 0; i < count; i++) {
        size_t oldest = (s_log_head + LOG_RECORDS - count + i) % LOG_RECORDS;
        records[i] = s_log[oldest];
    }
    portEXIT_CRITICAL(&s_state_mux);

    char line[192];
    for (size_t i = 0; i < count; i++) {
        csv_record_line(&records[i], line, sizeof(line));
        httpd_resp_sendstr_chunk(req, line);
    }
    return httpd_resp_sendstr_chunk(req, NULL);
}

// Return only the newest minute row, matching the "tail -f" style display use
// case for the small ESP32-S3 screen.
static esp_err_t latest_txt_handler(httpd_req_t *req)
{
    count_record_t record = {0};
    bool have_record;

    portENTER_CRITICAL(&s_state_mux);
    have_record = s_log_count > 0;
    if (have_record) {
        record = s_log[(s_log_head + LOG_RECORDS - 1) % LOG_RECORDS];
    }
    portEXIT_CRITICAL(&s_state_mux);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    if (!have_record) {
        return httpd_resp_sendstr(req, "waiting for first minute record\n");
    }

    char line[192];
    csv_record_line(&record, line, sizeof(line));
    return httpd_resp_sendstr(req, line);
}

// Accept browser time from the web page and repair the run start timestamp if
// the detector booted before time was known.
static esp_err_t time_handler(httpd_req_t *req)
{
    char value[24];
    if (!query_value(req, "epoch", value, sizeof(value))) {
        return text_response(req, "missing epoch");
    }
    long long epoch = atoll(value);
    if (epoch < 1600000000LL) {
        return text_response(req, "epoch too small");
    }
    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0,
    };
    ESP_RETURN_ON_ERROR(settimeofday(&tv, NULL), TAG, "set time");
    portENTER_CRITICAL(&s_state_mux);
    s_time_set = true;
    portEXIT_CRITICAL(&s_state_mux);
    // If the run started before the phone/browser set time, reconstruct the
    // likely boot timestamp and rename the active files to match it.
    if (s_run_start_epoch <= 1600000000 && s_run_start_uptime_ms > 0) {
        int64_t elapsed_s = ((esp_timer_get_time() / 1000) - s_run_start_uptime_ms) / 1000;
        s_run_start_epoch = (time_t)(epoch - elapsed_s);
        esp_err_t ret = sd_refresh_log_path();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SD log rename after time sync failed: %s", esp_err_to_name(ret));
        }
    }
    return text_response(req, "ok");
}

// Update the operator label that becomes part of the active SD filenames.
static esp_err_t run_label_handler(httpd_req_t *req)
{
    char label_raw[80] = "";
    char label_clean[RUN_LABEL_MAX + 1] = "";
    if (query_value(req, "label", label_raw, sizeof(label_raw))) {
        url_decode_in_place(label_raw);
        sanitize_run_label(label_raw, label_clean, sizeof(label_clean));
    }

    if (!s_sd_mutex) {
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    snprintf(s_run_label, sizeof(s_run_label), "%s", label_clean);
    esp_err_t ret = sd_refresh_log_path_locked();
    xSemaphoreGive(s_sd_mutex);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 SD rename failed");
        return text_response(req, esp_err_to_name(ret));
    }
    return text_response(req, "ok");
}

// Stream the active muon-count SD file to the browser.
static esp_err_t sd_csv_handler(httpd_req_t *req)
{
    char path[sizeof(s_log_path)] = "";
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(path, sizeof(path), "%s", s_log_path);
    xSemaphoreGive(s_sd_mutex);
    if (!mounted || !path[0]) {
        httpd_resp_set_status(req, "404 No SD log");
        return text_response(req, "no sd log");
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 SD file open failed");
        return text_response(req, "open failed");
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=muon_sd_log.csv");
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Stream the active environment SD file to the browser.
static esp_err_t env_csv_handler(httpd_req_t *req)
{
    char path[sizeof(s_env_log_path)] = "";
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(path, sizeof(path), "%s", s_env_log_path);
    xSemaphoreGive(s_sd_mutex);
    if (!mounted || !path[0]) {
        httpd_resp_set_status(req, "404 No environment SD log");
        return text_response(req, "no environment sd log");
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 Environment SD file open failed");
        return text_response(req, "open failed");
    }
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=environment_sd_log.csv");
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Return the filename portion of a full path.
static const char *path_basename(const char *path)
{
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

// Decide whether a requested SD filename is safe to list or download.
static bool is_log_filename_allowed(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len > SD_LOG_NAME_MAX || name[0] == '.') {
        return false;
    }
    for (size_t i = 0; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    const char *dot = strrchr(name, '.');
    return dot && (strcmp(dot, ".csv") == 0 || strcmp(dot, ".log") == 0);
}

#define SD_FILE_LIST_MAX 128

typedef struct {
    char name[SD_LOG_NAME_MAX + 1];
    off_t size;
    time_t mtime;
    bool current;
} sd_file_info_t;

// Sort SD files newest first, similar to "ls -lt".
static int compare_sd_file_info_newest_first(const void *a, const void *b)
{
    const sd_file_info_t *fa = (const sd_file_info_t *)a;
    const sd_file_info_t *fb = (const sd_file_info_t *)b;
    if (fa->mtime > fb->mtime) {
        return -1;
    }
    if (fa->mtime < fb->mtime) {
        return 1;
    }
    return strcmp(fa->name, fb->name);
}

// List previous CSV/log files on the SD card, newest first, for the dashboard.
static esp_err_t sd_files_handler(httpd_req_t *req)
{
    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }

    char current_muon_name[sizeof(s_log_path)] = "";
    char current_env_name[sizeof(s_env_log_path)] = "";
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    snprintf(current_muon_name, sizeof(current_muon_name), "%s", path_basename(s_log_path));
    snprintf(current_env_name, sizeof(current_env_name), "%s", path_basename(s_env_log_path));
    xSemaphoreGive(s_sd_mutex);
    if (!mounted) {
        httpd_resp_set_status(req, "404 SD not mounted");
        return text_response(req, "sd not mounted");
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        httpd_resp_set_status(req, "500 SD open dir failed");
        return text_response(req, "open dir failed");
    }

    sd_file_info_t *files = calloc(SD_FILE_LIST_MAX, sizeof(sd_file_info_t));
    if (!files) {
        closedir(dir);
        httpd_resp_set_status(req, "500 Out of memory");
        return text_response(req, "out of memory");
    }
    size_t file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_log_filename_allowed(entry->d_name)) {
            continue;
        }

        char name[SD_LOG_NAME_MAX + 1];
        size_t name_len = strlen(entry->d_name);
        memcpy(name, entry->d_name, name_len + 1);
        char path[sizeof(s_log_path)];
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        sd_file_info_t *file = NULL;
        if (file_count < SD_FILE_LIST_MAX) {
            file = &files[file_count++];
        } else {
            // The browser only needs a useful recent list. If the card has many
            // files, keep the newest entries instead of allocating a huge buffer.
            size_t oldest = 0;
            for (size_t i = 1; i < file_count; i++) {
                if (files[i].mtime < files[oldest].mtime) {
                    oldest = i;
                }
            }
            if (st.st_mtime <= files[oldest].mtime) {
                continue;
            }
            file = &files[oldest];
        }
        snprintf(file->name, sizeof(file->name), "%s", name);
        file->size = st.st_size;
        file->mtime = st.st_mtime;
        file->current = strcmp(name, current_muon_name) == 0 || strcmp(name, current_env_name) == 0;
    }
    closedir(dir);

    qsort(files, file_count, sizeof(files[0]), compare_sd_file_info_newest_first);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, "{\"files\":[");

    for (size_t i = 0; i < file_count; i++) {
        char modified[32] = "unknown";
        if (files[i].mtime > 1600000000) {
            csv_time_string(files[i].mtime, modified, sizeof(modified));
        }
        char item[320];
        snprintf(item, sizeof(item),
                 "%s{\"name\":\"%s\",\"modified\":\"%s\",\"mtime\":%lld,\"size\":%lld,\"current\":%s}",
                 i == 0 ? "" : ",",
                 files[i].name,
                 modified,
                 (long long)files[i].mtime,
                 (long long)files[i].size,
                 files[i].current ? "true" : "false");
        httpd_resp_sendstr_chunk(req, item);
    }
    free(files);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_sendstr_chunk(req, NULL);
}

// Stream a selected previous SD file after validating the requested name.
static esp_err_t sd_file_handler(httpd_req_t *req)
{
    char name_raw[96];
    if (!query_value(req, "name", name_raw, sizeof(name_raw))) {
        return text_response(req, "missing name");
    }
    url_decode_in_place(name_raw);
    if (!is_log_filename_allowed(name_raw)) {
        httpd_resp_set_status(req, "400 Bad file name");
        return text_response(req, "bad file name");
    }

    if (!s_sd_mutex) {
        httpd_resp_set_status(req, "404 SD not initialized");
        return text_response(req, "sd not initialized");
    }
    xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    bool mounted = s_sd_mounted;
    xSemaphoreGive(s_sd_mutex);
    if (!mounted) {
        httpd_resp_set_status(req, "404 SD not mounted");
        return text_response(req, "sd not mounted");
    }

    char path[sizeof(s_log_path)];
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/%s", name_raw);
    FILE *f = fopen(path, "r");
    if (!f) {
        httpd_resp_set_status(req, "404 SD file open failed");
        return text_response(req, "open failed");
    }

    char disposition[160];
    snprintf(disposition, sizeof(disposition), "attachment; filename=%s", name_raw);
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    char chunk[512];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

// Web endpoint for setting or disabling HV, using the same settle gating as
// startup.
static esp_err_t hv_handler(httpd_req_t *req)
{
    char value[16];
    if (query_value(req, "off", value, sizeof(value))) {
        ESP_RETURN_ON_ERROR(hv_write_and_settle(0x00), TAG, "HV off");
        return text_response(req, "ok");
    }
    if (!query_value(req, "byte", value, sizeof(value))) {
        return text_response(req, "missing byte");
    }
    unsigned byte;
    if (sscanf(value, "%x", &byte) != 1 || byte > 0xff) {
        return text_response(req, "bad byte");
    }
    ESP_RETURN_ON_ERROR(hv_write_and_settle((uint8_t)byte), TAG, "HV set");
    return text_response(req, "ok");
}

// Web endpoint for writing one DAC channel during bench tuning.
static esp_err_t dac_handler(httpd_req_t *req)
{
    char ch_s[8];
    char value_s[16];
    if (!query_value(req, "ch", ch_s, sizeof(ch_s)) || !query_value(req, "value", value_s, sizeof(value_s))) {
        return text_response(req, "missing ch or value");
    }
    unsigned ch;
    unsigned value;
    if (sscanf(ch_s, "%u", &ch) != 1 || sscanf(value_s, "%x", &value) != 1 || ch > 7 || value > 0xffff) {
        return text_response(req, "bad dac parameters");
    }
    ESP_RETURN_ON_ERROR(dac_set_channel((uint8_t)ch, (uint16_t)value), TAG, "DAC write");
    return text_response(req, "ok");
}

// Web endpoint for the safe FPGA reflash sequence: HV off, program, DAC init,
// HV back on, then normal settle.
static esp_err_t fpga_handler(httpd_req_t *req)
{
    // Reflashing while biased made the front-end LEDs and counts misbehave on
    // the bench, so enforce the safe sequence here instead of trusting the UI.
    ESP_LOGW(TAG, "FPGA reflash requested: turning HV off before programming");
    esp_err_t ret = hv_write_and_settle(0x00);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 HV off failed");
        return text_response(req, esp_err_to_name(ret));
    }

    clear_live_counts();
    ret = program_fpga();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 FPGA program failed");
        return text_response(req, esp_err_to_name(ret));
    }
    ret = dac_zero_channels();
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 DAC startup failed");
        return text_response(req, esp_err_to_name(ret));
    }
    ret = hv_write_and_settle(STARTUP_HV_BYTE);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "500 HV restart failed");
        return text_response(req, esp_err_to_name(ret));
    }
    return text_response(req, "ok: FPGA flashed, DAC startup values loaded, HV restarted");
}

// Lower the idle CPU floor where ESP-IDF power management is available.
static void set_runtime_power_profile(void)
{
#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = FIELD_MIN_CPU_FREQ_MHZ,
        .light_sleep_enable = false,
    };
    esp_err_t ret = esp_pm_configure(&pm_config);
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "PM allows idle CPU scaling down to %d MHz for power saving", FIELD_MIN_CPU_FREQ_MHZ);
    } else {
        ESP_LOGW(TAG, "CPU frequency limit failed: %s", esp_err_to_name(ret));
    }
#else
    ESP_LOGW(TAG, "CPU frequency limit unavailable; CONFIG_PM_ENABLE is disabled");
#endif
}

// Shut the radio stack down for quiet battery operation, briefly cycling HV off
// before restoring the detector.
static void power_save_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));

    // Capture the current HV byte before the web server disappears. After this
    // request returns, the operator may not be able to reach the device again.
    uint8_t restore_hv;
    portENTER_CRITICAL(&s_state_mux);
    restore_hv = s_hv_byte;
    portEXIT_CRITICAL(&s_state_mux);

    ESP_LOGW(TAG, "power saving mode: turning HV off before stopping Wi-Fi");
    esp_err_t ret = hv_write_and_settle(0x00);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HV off before Wi-Fi shutdown failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "power saving mode: stopping HTTP server and Wi-Fi until next reboot");
    if (s_httpd) {
        httpd_handle_t httpd = s_httpd;
        s_httpd = NULL;
        ret = httpd_stop(httpd);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HTTP server stop failed: %s", esp_err_to_name(ret));
        }
    }

    ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wi-Fi stop failed: %s", esp_err_to_name(ret));
    }
    ret = esp_wifi_deinit();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGW(TAG, "Wi-Fi deinit failed: %s", esp_err_to_name(ret));
    }

    ESP_LOGW(TAG, "Wi-Fi is off; keeping HV off for 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000));
    // The short HV-off window was added after Wi-Fi activity showed up as noise.
    // Counting is still gated by the normal HV settle delay when HV comes back.
    if (restore_hv != 0) {
        ESP_LOGW(TAG, "restoring HV byte 0x%02x after Wi-Fi shutdown", restore_hv);
        ret = hv_write_and_settle(restore_hv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "HV restore after Wi-Fi shutdown failed: %s", esp_err_to_name(ret));
        }
    } else {
        ESP_LOGW(TAG, "HV was already off before Wi-Fi shutdown; leaving it off");
    }

    set_runtime_power_profile();
    esp_log_level_set("*", ESP_LOG_WARN);
    ESP_LOGW(TAG, "power saving mode active; Wi-Fi is off and detector logging continues");
    vTaskDelete(NULL);
}

// Mark power-saving mode active and launch the shutdown task once.
static esp_err_t enter_power_save_mode(void)
{
    portENTER_CRITICAL(&s_state_mux);
    bool already_enabled = s_power_save_mode;
    s_power_save_mode = true;
    portEXIT_CRITICAL(&s_state_mux);

    if (already_enabled) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(power_save_task, "power_save_task", 4096, NULL, 5, NULL, 1);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

// Respond to the browser before Wi-Fi disappears, then start power-saving mode.
static esp_err_t power_save_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    ESP_RETURN_ON_ERROR(httpd_resp_sendstr(req, "ok: Wi-Fi shutting down until next reboot"), TAG, "send power-save response");

    esp_err_t ret = enter_power_save_mode();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "could not enter power saving mode: %s", esp_err_to_name(ret));
    }
    return ESP_OK;
}

// Toggle whether Wi-Fi should stay up for this boot.
static esp_err_t wifi_keep_on_handler(httpd_req_t *req)
{
    char value[8] = "1";
    query_value(req, "enable", value, sizeof(value));
    bool keep_on = strcmp(value, "0") != 0 && strcasecmp(value, "false") != 0;

    portENTER_CRITICAL(&s_state_mux);
    s_wifi_keep_on = keep_on;
    portEXIT_CRITICAL(&s_state_mux);

    ESP_LOGW(TAG, "Wi-Fi auto-off %s", keep_on ? "disabled" : "enabled");
    return text_response(req, keep_on ? "ok: Wi-Fi will stay on" : "ok: Wi-Fi auto-off enabled");
}

// Give the operator a short setup window, then disable Wi-Fi automatically if no
// one connected and "keep on" was not selected.
void auto_power_save_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(WIFI_AUTO_OFF_MS));

    while (true) {
        portENTER_CRITICAL(&s_state_mux);
        bool active = s_power_save_mode;
        bool keep_on = s_wifi_keep_on;
        uint8_t clients = s_wifi_clients;
        portEXIT_CRITICAL(&s_state_mux);

        if (active) {
            break;
        }
        if (keep_on) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        if (clients == 0) {
            // Field default: give the operator a short setup window, then shut
            // radio noise down if nobody connected.
            ESP_LOGW(TAG, "no Wi-Fi client after setup window; entering power saving mode");
            esp_err_t ret = enter_power_save_mode();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "auto power saving failed: %s", esp_err_to_name(ret));
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
    vTaskDelete(NULL);
}

// Track AP client count so auto power-save knows whether anyone is using Wi-Fi.
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_data;
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        portENTER_CRITICAL(&s_state_mux);
        if (s_wifi_clients < UINT8_MAX) {
            s_wifi_clients++;
        }
        portEXIT_CRITICAL(&s_state_mux);
        ESP_LOGI(TAG, "Wi-Fi client connected");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        portENTER_CRITICAL(&s_state_mux);
        if (s_wifi_clients > 0) {
            s_wifi_clients--;
        }
        portEXIT_CRITICAL(&s_state_mux);
        ESP_LOGI(TAG, "Wi-Fi client disconnected");
    }
}

// Start the ESP32 access point used for local detector setup and downloads.
esp_err_t start_wifi_ap(void)
{
    ESP_LOGI(TAG, "Wi-Fi init: netif");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_LOGI(TAG, "Wi-Fi init: event loop");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    ESP_LOGI(TAG, "Wi-Fi init: AP netif");
    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_LOGI(TAG, "Wi-Fi init: event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), TAG, "wifi event");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "Wi-Fi init: esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_LOGI(TAG, "Wi-Fi init: storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage");

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), "%s", WIFI_AP_SSID);
    snprintf((char *)wifi_config.ap.password, sizeof(wifi_config.ap.password), "%s", WIFI_AP_PASS);
    wifi_config.ap.ssid_len = strlen(WIFI_AP_SSID);
    wifi_config.ap.channel = WIFI_AP_CHANNEL;
    wifi_config.ap.max_connection = 1;
    wifi_config.ap.beacon_interval = 1000;
    wifi_config.ap.authmode = strlen(WIFI_AP_PASS) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_LOGI(TAG, "Wi-Fi init: AP mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode AP");
    ESP_LOGI(TAG, "Wi-Fi init: AP config");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &wifi_config), TAG, "wifi AP config");
    ESP_LOGI(TAG, "Wi-Fi init: AP bandwidth");
    ESP_RETURN_ON_ERROR(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20), TAG, "wifi AP bandwidth");
    ESP_LOGI(TAG, "Wi-Fi init: start AP");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
    ESP_LOGI(TAG, "Wi-Fi AP started: SSID=%s password=%s URL=http://192.168.4.1", WIFI_AP_SSID, WIFI_AP_PASS);
    return ESP_OK;
}

// Start the HTTP server and register all dashboard/API routes.
esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Keep the web server on the non-counter core with modest priority. It is a
    // convenience interface; GPIO counting is the measurement.
    config.core_id = 1;
    config.task_priority = tskIDLE_PRIORITY + 3;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    config.lru_purge_enable = true;

    ESP_RETURN_ON_ERROR(httpd_start(&s_httpd, &config), TAG, "start HTTP server");
    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root_handler},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/latest.txt", .method = HTTP_GET, .handler = latest_txt_handler},
        {.uri = "/api/log.csv", .method = HTTP_GET, .handler = log_csv_handler},
        {.uri = "/api/sd.csv", .method = HTTP_GET, .handler = sd_csv_handler},
        {.uri = "/api/env.csv", .method = HTTP_GET, .handler = env_csv_handler},
        {.uri = "/api/sd_files", .method = HTTP_GET, .handler = sd_files_handler},
        {.uri = "/api/sd_file", .method = HTTP_GET, .handler = sd_file_handler},
        {.uri = "/api/time", .method = HTTP_GET, .handler = time_handler},
        {.uri = "/api/run_label", .method = HTTP_GET, .handler = run_label_handler},
        {.uri = "/api/hv", .method = HTTP_GET, .handler = hv_handler},
        {.uri = "/api/dac", .method = HTTP_GET, .handler = dac_handler},
        {.uri = "/api/fpga", .method = HTTP_GET, .handler = fpga_handler},
        {.uri = "/api/power_save", .method = HTTP_GET, .handler = power_save_handler},
        {.uri = "/api/wifi_keep_on", .method = HTTP_GET, .handler = wifi_keep_on_handler},
    };
    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_httpd, &handlers[i]), TAG, "register HTTP handler");
    }
    ESP_LOGI(TAG, "HTTP server ready on http://192.168.4.1");
    return ESP_OK;
}
