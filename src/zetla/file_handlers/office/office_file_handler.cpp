#include "office_file_handler.hpp"
#include "../base/file_handler_factory.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <set>
#include "zip.h"

namespace zetla::file_handlers {

    static const char b64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    static std::string base64_encode(const std::vector<uint8_t>& data) {
        std::string result;
        result.reserve(((data.size() + 2) / 3) * 4);
        for (size_t i = 0; i < data.size(); i += 3) {
            uint32_t n = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < data.size()) n |= static_cast<uint32_t>(data[i + 2]);
            result += b64_table[(n >> 18) & 0x3F];
            result += b64_table[(n >> 12) & 0x3F];
            result += (i + 1 < data.size()) ? b64_table[(n >> 6) & 0x3F] : '=';
            result += (i + 2 < data.size()) ? b64_table[n & 0x3F] : '=';
        }
        return result;
    }

    static std::string strip_xml_tags(const std::string& xml) {
        std::string result;
        bool in_tag = false;
        bool in_comment = false;
        for (size_t i = 0; i < xml.size(); i++) {
            if (!in_comment && xml[i] == '<' && i + 3 < xml.size() &&
                xml[i+1] == '!' && xml[i+2] == '-' && xml[i+3] == '-') {
                in_comment = true;
                continue;
            }
            if (in_comment && xml[i] == '-' && i + 2 < xml.size() &&
                xml[i+1] == '-' && xml[i+2] == '>') {
                in_comment = false;
                i += 2;
                continue;
            }
            if (xml[i] == '<') {
                in_tag = true;
                continue;
            }
            if (xml[i] == '>') {
                in_tag = false;
                continue;
            }
            if (!in_tag && !in_comment) {
                result += xml[i];
            }
        }
        return result;
    }

    static std::string extract_xml_tag_content(const std::string& xml, const std::string& tag) {
        std::string result;
        std::string open_tag = "<" + tag + ">";
        std::string close_tag = "</" + tag + ">";
        size_t pos = 0;
        while (true) {
            size_t start = xml.find(open_tag, pos);
            if (start == std::string::npos) break;
            start += open_tag.size();
            size_t end = xml.find(close_tag, start);
            if (end == std::string::npos) break;
            result += xml.substr(start, end - start);
            pos = end + close_tag.size();
        }
        return result;
    }

    static std::string extract_xml_tag_content_with_attrs(const std::string& xml, const std::string& tag) {
        std::string result;
        std::string open_tag = "<" + tag;
        std::string close_tag = "</" + tag + ">";
        size_t pos = 0;
        while (true) {
            size_t start = xml.find(open_tag, pos);
            if (start == std::string::npos) break;
            size_t gt = xml.find('>', start);
            if (gt == std::string::npos) break;
            if (xml[gt - 1] == '/') {
                pos = gt + 1;
                continue;
            }
            start = gt + 1;
            size_t end = xml.find(close_tag, start);
            if (end == std::string::npos) break;
            result += xml.substr(start, end - start);
            pos = end + close_tag.size();
        }
        return result;
    }

    static std::string get_xml_attr(const std::string& tag_str, const std::string& attr_name) {
        std::string target = attr_name + "=\"";
        size_t pos = tag_str.find(target);
        if (pos == std::string::npos) return "";
        pos += target.size();
        size_t end = tag_str.find('"', pos);
        if (end == std::string::npos) return "";
        return tag_str.substr(pos, end - pos);
    }

    static std::vector<std::string> find_all_xml_tags(const std::string& xml, const std::string& tag) {
        std::vector<std::string> results;
        std::string open_tag = "<" + tag;
        std::string close_tag = "</" + tag + ">";
        size_t pos = 0;
        while (true) {
            size_t start = xml.find(open_tag, pos);
            if (start == std::string::npos) break;
            size_t gt = xml.find('>', start);
            if (gt == std::string::npos) break;
            if (xml[gt - 1] == '/') {
                pos = gt + 1;
                continue;
            }
            size_t content_start = gt + 1;
            size_t end = xml.find(close_tag, content_start);
            if (end == std::string::npos) break;
            results.push_back(xml.substr(start, end + close_tag.size() - start));
            pos = end + close_tag.size();
        }
        return results;
    }

    static std::string html_entity_decode(const std::string& s) {
        std::string result;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '&') {
                if (s.substr(i, 5) == "&amp;") { result += '&'; i += 4; }
                else if (s.substr(i, 4) == "&lt;") { result += '<'; i += 3; }
                else if (s.substr(i, 4) == "&gt;") { result += '>'; i += 3; }
                else if (s.substr(i, 5) == "&nbsp;") { result += ' '; i += 5; }
                else if (s.substr(i, 6) == "&quot;") { result += '"'; i += 5; }
                else if (s.substr(i, 3) == "&apos;") { result += '\''; i += 4; }
                else result += s[i];
            } else {
                result += s[i];
            }
        }
        return result;
    }

    static std::string read_zip_entry_by_name(struct zip_t *zip, const char *entryname) {
        if (zip_entry_open(zip, entryname) < 0) return "";
        void *buf = NULL;
        size_t bufsize = 0;
        ssize_t len = zip_entry_read(zip, &buf, &bufsize);
        std::string result;
        if (len > 0 && buf) {
            result.assign(static_cast<char*>(buf), static_cast<size_t>(len));
        }
        if (buf) free(buf);
        return result;
    }

    std::string OfficeFileHandler::mime_type() const {
        return "application/vnd.openxmlformats-officedocument";
    }

    FileContentType OfficeFileHandler::content_type() const {
        return FileContentType::STRUCTURED;
    }

    FileContent OfficeFileHandler::extract(const std::string& file_path) {
        std::string ext = get_extension(file_path);
        std::string name = extract_file_name(file_path);

        if (ext == ".docx") return extract_docx(file_path, name);
        if (ext == ".xlsx") return extract_xlsx(file_path, name);
        if (ext == ".pptx") return extract_pptx(file_path, name);
        if (ext == ".csv") return extract_csv(file_path, name);
        if (ext == ".md") return extract_markdown(file_path, name);
        if (ext == ".rtf") return extract_rtf(file_path, name);

        FileContent result;
        result.file_name = name;
        result.mime_type = detect_mime_type(file_path);
        result.type = FileContentType::UNSUPPORTED;
        return result;
    }

    // -- DOCX extraction --

    FileContent OfficeFileHandler::extract_docx(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "application/vnd.openxmlformats-officedocument.wordprocessingml.document";

        struct zip_t *zip = zip_open(file_path.c_str(), 0, 'r');
        if (!zip) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        std::string doc_xml = read_zip_entry_by_name(zip, "word/document.xml");
        if (doc_xml.empty()) {
            zip_close(zip);
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        zip_close(zip);

        std::ostringstream text;
        auto paragraphs = find_all_xml_tags(doc_xml, "w:p");
        for (size_t pi = 0; pi < paragraphs.size(); pi++) {
            auto& p = paragraphs[pi];

            auto tbl = p.find("<w:tbl");
            if (tbl != std::string::npos) {
                auto rows = find_all_xml_tags(p, "w:tr");
                for (size_t ri = 0; ri < rows.size(); ri++) {
                    auto cells = find_all_xml_tags(rows[ri], "w:tc");
                    for (size_t ci = 0; ci < cells.size(); ci++) {
                        if (ci > 0) text << " | ";
                        std::string cell_text = extract_xml_tag_content_with_attrs(cells[ci], "w:t");
                        cell_text = html_entity_decode(strip_xml_tags(cell_text));
                        text << cell_text;
                    }
                    text << "\n";
                    if (ri == 0) {
                        for (size_t ci = 0; ci < cells.size(); ci++) {
                            if (ci > 0) text << " | ";
                            text << "---";
                        }
                        text << "\n";
                    }
                }
            } else {
                auto runs = find_all_xml_tags(p, "w:r");
                for (auto& r : runs) {
                    std::string t = extract_xml_tag_content_with_attrs(r, "w:t");
                    t = html_entity_decode(strip_xml_tags(t));
                    text << t;

                    if (r.find("<w:br/>") != std::string::npos || r.find("<w:br ") != std::string::npos) {
                        text << "\n";
                    }
                }

                size_t tab_pos = 0;
                std::string p_str = p;
                while ((tab_pos = p_str.find("<w:tab/>", tab_pos)) != std::string::npos) {
                    text << "\t";
                    tab_pos += 8;
                }
            }

            if (pi + 1 < paragraphs.size()) {
                text << "\n";
            }
        }

        result.text_content = text.str();
        result.type = FileContentType::STRUCTURED;

        size_t para_count = 0;
        for (char c : result.text_content) {
            if (c == '\n') para_count++;
        }
        result.metadata["paragraph_count"] = std::to_string(para_count);

        return result;
    }

    // -- XLSX extraction --

    FileContent OfficeFileHandler::extract_xlsx(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";

        struct zip_t *zip = zip_open(file_path.c_str(), 0, 'r');
        if (!zip) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        std::vector<std::string> shared_strings;
        std::string ss_xml = read_zip_entry_by_name(zip, "xl/sharedStrings.xml");
        if (!ss_xml.empty()) {
            auto si_tags = find_all_xml_tags(ss_xml, "si");
            for (auto& si : si_tags) {
                std::string t = extract_xml_tag_content_with_attrs(si, "t");
                shared_strings.push_back(html_entity_decode(strip_xml_tags(t)));
            }
        }

        std::map<std::string, std::string> sheet_names;
        std::string wb_xml = read_zip_entry_by_name(zip, "xl/workbook.xml");
        if (!wb_xml.empty()) {
            auto sheets = find_all_xml_tags(wb_xml, "sheet");
            for (auto& sh : sheets) {
                std::string rid = get_xml_attr(sh, "r:id");
                std::string sname = get_xml_attr(sh, "name");
                if (!rid.empty() && !sname.empty()) {
                    sheet_names[rid] = html_entity_decode(sname);
                }
            }
        }

        std::map<std::string, std::string> rid_to_file;
        std::string rel_xml = read_zip_entry_by_name(zip, "xl/_rels/workbook.xml.rels");
        if (!rel_xml.empty()) {
            auto rels = find_all_xml_tags(rel_xml, "Relationship");
            for (auto& r : rels) {
                std::string id = get_xml_attr(r, "Id");
                std::string target = get_xml_attr(r, "Target");
                if (!id.empty() && !target.empty()) {
                    rid_to_file[id] = "xl/" + target;
                }
            }
        }

        int total = zip_total_entries(zip);
        std::ostringstream csv;
        int sheet_count = 0;

        for (int i = 0; i < total; i++) {
            if (zip_entry_openbyindex(zip, i) < 0) continue;
            const char *entry_name = zip_entry_name(zip);
            if (!entry_name) { zip_entry_close(zip); continue; }

            std::string ename(entry_name);
            if (ename.find("xl/worksheets/sheet") != 0 || ename.find(".xml") == std::string::npos) {
                zip_entry_close(zip);
                continue;
            }

            sheet_count++;
            void *buf = NULL;
            size_t bufsize = 0;
            ssize_t len = zip_entry_read(zip, &buf, &bufsize);
            std::string sheet_xml;
            if (len > 0 && buf) sheet_xml.assign(static_cast<char*>(buf), static_cast<size_t>(len));
            if (buf) free(buf);
            zip_entry_close(zip);

            std::string sheet_name = "Sheet" + std::to_string(sheet_count);
            for (auto& [rid, f] : rid_to_file) {
                if (f == ename) {
                    auto it = sheet_names.find(rid);
                    if (it != sheet_names.end()) sheet_name = it->second;
                    break;
                }
            }

            if (sheet_count > 1) csv << "\n";
            csv << "=== " << sheet_name << " ===\n";

            std::map<int, std::map<int, std::string>> grid;
            int max_row = 0;
            int max_col = 0;

            auto rows = find_all_xml_tags(sheet_xml, "row");
            for (auto& row_tag : rows) {
                auto cells = find_all_xml_tags(row_tag, "c");
                for (auto& cell : cells) {
                    std::string cell_ref = get_xml_attr(cell, "r");
                    if (cell_ref.empty()) continue;

                    int col = 0;
                    size_t ci = 0;
                    while (ci < cell_ref.size() && !std::isdigit(static_cast<unsigned char>(cell_ref[ci]))) {
                        col = col * 26 + (cell_ref[ci] - 'A' + 1);
                        ci++;
                    }
                    col--;

                    int row = 0;
                    while (ci < cell_ref.size() && std::isdigit(static_cast<unsigned char>(cell_ref[ci]))) {
                        row = row * 10 + (cell_ref[ci] - '0');
                        ci++;
                    }

                    std::string t = get_xml_attr(cell, "t");
                    std::string v = extract_xml_tag_content(cell, "v");

                    std::string value;
                    if (t == "s" && !v.empty()) {
                        size_t idx = std::stoul(v);
                        if (idx < shared_strings.size()) value = shared_strings[idx];
                    } else {
                        value = v;
                    }

                    if (!value.empty()) {
                        if (value.find(',') != std::string::npos || value.find('"') != std::string::npos) {
                            std::string escaped = value;
                            size_t q = 0;
                            while ((q = escaped.find('"', q)) != std::string::npos) {
                                escaped.insert(q, "\"");
                                q += 2;
                            }
                            value = "\"" + escaped + "\"";
                        }
                    }

                    grid[row][col] = value;
                    if (row > max_row) max_row = row;
                    if (col > max_col) max_col = col;
                }
            }

            for (int r = 1; r <= max_row; r++) {
                for (int c = 0; c <= max_col; c++) {
                    if (c > 0) csv << ",";
                    auto it = grid.find(r);
                    if (it != grid.end()) {
                        auto cit = it->second.find(c);
                        if (cit != it->second.end()) csv << cit->second;
                    }
                }
                csv << "\n";
            }
        }

        result.text_content = csv.str();
        result.type = FileContentType::STRUCTURED;
        result.metadata["sheet_count"] = std::to_string(sheet_count);

        size_t row_count = 0;
        for (char c : result.text_content) {
            if (c == '\n') row_count++;
        }
        result.metadata["row_count"] = std::to_string(row_count);

        return result;
    }

    // -- PPTX extraction --

    FileContent OfficeFileHandler::extract_pptx(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "application/vnd.openxmlformats-officedocument.presentationml.presentation";

        struct zip_t *zip = zip_open(file_path.c_str(), 0, 'r');
        if (!zip) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        int total = zip_total_entries(zip);
        std::ostringstream text;
        int slide_num = 0;

        for (int i = 0; i < total; i++) {
            if (zip_entry_openbyindex(zip, i) < 0) continue;
            const char *entry_name = zip_entry_name(zip);
            if (!entry_name) { zip_entry_close(zip); continue; }

            std::string ename(entry_name);
            if (ename.find("ppt/slides/slide") != 0 || ename.find(".xml") == std::string::npos) {
                zip_entry_close(zip);
                continue;
            }

            slide_num++;
            void *buf = NULL;
            size_t bufsize = 0;
            ssize_t len = zip_entry_read(zip, &buf, &bufsize);
            std::string xml;
            if (len > 0 && buf) xml.assign(static_cast<char*>(buf), static_cast<size_t>(len));
            if (buf) free(buf);
            zip_entry_close(zip);

            if (xml.empty()) continue;

            text << "--- Slide " << slide_num << " ---\n";

            auto a_t_tags = find_all_xml_tags(xml, "a:t");
            for (auto& atag : a_t_tags) {
                std::string t = strip_xml_tags(atag);
                t = html_entity_decode(t);
                if (!t.empty()) text << t;
            }

            text << "\n\n";
        }

        if (slide_num == 0) {
            zip_close(zip);
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        result.text_content = text.str();
        result.type = FileContentType::STRUCTURED;

        std::string image_data_uri;
        for (int i = 0; i < total; i++) {
            if (zip_entry_openbyindex(zip, i) < 0) continue;
            const char *entry_name = zip_entry_name(zip);
            if (!entry_name) { zip_entry_close(zip); continue; }

            std::string ename(entry_name);
            if (ename.find("ppt/media/") != 0) { zip_entry_close(zip); continue; }

            void *buf = NULL;
            size_t bufsize = 0;
            ssize_t len = zip_entry_read(zip, &buf, &bufsize);
            if (len > 0 && buf) {
                std::string data(static_cast<char*>(buf), static_cast<size_t>(len));
                std::string img_b64 = base64_encode(
                    std::vector<uint8_t>(data.begin(), data.end()));

                std::string img_mime = "image/png";
                std::string lower = to_lower(ename);
                if (lower.find(".jpg") != std::string::npos || lower.find(".jpeg") != std::string::npos)
                    img_mime = "image/jpeg";
                else if (lower.find(".gif") != std::string::npos)
                    img_mime = "image/gif";

                if (!image_data_uri.empty()) image_data_uri += "\n---\n";
                image_data_uri += "data:" + img_mime + ";base64," + img_b64;
            }
            if (buf) free(buf);
            zip_entry_close(zip);
        }

        result.image_data_uri = image_data_uri;
        result.metadata["slide_count"] = std::to_string(slide_num);

        zip_close(zip);
        return result;
    }

    // -- CSV extraction --

    FileContent OfficeFileHandler::extract_csv(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "text/csv";

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        file.seekg(0, std::ios::end);
        result.size_bytes = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::ostringstream ss;
        ss << file.rdbuf();
        result.text_content = ss.str();
        result.type = FileContentType::STRUCTURED;

        size_t row_count = 0;
        size_t col_count = 0;
        bool first_row = true;
        size_t pos = 0;
        while (pos < result.text_content.size()) {
            size_t nl = result.text_content.find('\n', pos);
            if (nl == std::string::npos) nl = result.text_content.size();

            std::string line = result.text_content.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (!line.empty()) {
                row_count++;
                if (first_row) {
                    size_t c = 0;
                    bool in_quotes = false;
                    for (char ch : line) {
                        if (ch == '"') in_quotes = !in_quotes;
                        else if (ch == ',' && !in_quotes) c++;
                    }
                    col_count = c + 1;
                    first_row = false;
                }
            }

            pos = nl + 1;
        }

        result.metadata["row_count"] = std::to_string(row_count);
        result.metadata["column_count"] = std::to_string(col_count);

        return result;
    }

    // -- Markdown extraction --

    FileContent OfficeFileHandler::extract_markdown(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "text/markdown";

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        file.seekg(0, std::ios::end);
        result.size_bytes = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::ostringstream ss;
        ss << file.rdbuf();
        result.text_content = ss.str();
        result.type = FileContentType::TEXT;

        size_t lines = 0;
        for (char c : result.text_content) {
            if (c == '\n') lines++;
        }
        result.metadata["line_count"] = std::to_string(lines);

        return result;
    }

    // -- RTF extraction --

    FileContent OfficeFileHandler::extract_rtf(const std::string& file_path, const std::string& name) {
        FileContent result;
        result.file_name = name;
        result.mime_type = "application/rtf";

        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        file.seekg(0, std::ios::end);
        result.size_bytes = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        std::string rtf;
        {
            std::ostringstream ss;
            ss << file.rdbuf();
            rtf = ss.str();
        }

        if (rtf.empty()) {
            result.type = FileContentType::UNSUPPORTED;
            return result;
        }

        std::ostringstream text;
        size_t i = 0;
        bool skip_group = false;
        int group_skip_depth = 0;
        bool in_hex = false;
        std::string hex_buf;

        while (i < rtf.size()) {
            char c = rtf[i];

            if (in_hex) {
                if (std::isxdigit(static_cast<unsigned char>(c))) {
                    hex_buf += c;
                    if (hex_buf.size() == 2) {
                        char val = static_cast<char>(std::stoul(hex_buf, nullptr, 16));
                        text << val;
                        hex_buf.clear();
                        in_hex = false;
                    }
                } else {
                    in_hex = false;
                    hex_buf.clear();
                }
                i++;
                continue;
            }

            if (c == '{') {
                if (skip_group) group_skip_depth++;
                i++;
                continue;
            }

            if (c == '}') {
                if (skip_group && group_skip_depth > 0) {
                    group_skip_depth--;
                    if (group_skip_depth == 0) skip_group = false;
                }
                i++;
                continue;
            }

            if (c == '\\') {
                i++;
                if (i >= rtf.size()) break;

                char next = rtf[i];

                if (next == '\'') {
                    in_hex = true;
                    hex_buf.clear();
                    i++;
                    continue;
                }

                if (next == '\\') { text << '\\'; i++; continue; }
                if (next == '{') { text << '{'; i++; continue; }
                if (next == '}') { text << '}'; i++; continue; }
                if (next == '~') { text << '\xA0'; i++; continue; }
                if (next == '-') { text << '-'; i++; continue; }
                if (next == '_') { text << '-'; i++; continue; }
                if (next == '\n' || next == '\r') { i++; continue; }
                if (next == ':') { i++; continue; }
                if (next == '*') {
                    skip_group = true;
                    group_skip_depth = 0;
                    i++;
                    continue;
                }
                if (next == 'p' && i + 3 < rtf.size() && rtf[i+1] == 'a' &&
                    rtf[i+2] == 'r' && !std::isalpha(static_cast<unsigned char>(rtf[i+3]))) {
                    text << '\n';
                    i += 3;
                    continue;
                }
                if (next == 'l' && i + 2 < rtf.size() && rtf[i+1] == 'i' &&
                    rtf[i+2] == 'n' && !std::isalpha(static_cast<unsigned char>(rtf[i+3]))) {
                    text << '\n';
                    i += 3;
                    continue;
                }
                if (next == 't' && !std::isalpha(static_cast<unsigned char>(rtf[i+1]))) {
                    text << '\t';
                    i++;
                    continue;
                }

                if (std::isalpha(static_cast<unsigned char>(next))) {
                    while (i < rtf.size() && std::isalpha(static_cast<unsigned char>(rtf[i]))) i++;
                    if (i < rtf.size() && rtf[i] == ' ') i++;
                }

                continue;
            }

            if (!skip_group && c != '\r' && c != '\n') {
                text << c;
            }
            i++;
        }

        result.text_content = text.str();
        result.type = FileContentType::STRUCTURED;

        size_t lines = 0;
        for (char c : result.text_content) {
            if (c == '\n') lines++;
        }
        result.metadata["line_count"] = std::to_string(lines);

        return result;
    }

}
