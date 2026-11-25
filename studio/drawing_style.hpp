#pragma once

#include "next/core/document.hpp"
#include <array>
#include <cmath>
#include <cwchar>
#include <string>
#include <utility>
#include <vector>

namespace studio_drawing {
// Numeric style facts are audited in docs/JOURNAL_DRAWING_STYLE_AUDIT.txt.
struct JournalStyle {
 const wchar_t* name;double bondLength,lineWidth,boldWidth,marginWidth,hashSpacing,bondSpacing,chainAngle;
 const wchar_t* font;double fontSize;const wchar_t* captionFont;double captionSize;const wchar_t* source;
};
inline const std::vector<JournalStyle>& journalStyles(){static const std::vector<JournalStyle> styles={
        {L"ACS Document 1996",14.4,0.6,2,1.6,2.5,18,120,L"Arial",10,L"Arial",10,L"ACS 1996；已对照 ACS 作者指南与 ChemDraw 手册"},
        {L"Nature Research",10.8,0.595275591,1.55905512,1.19055118,1.7007874,18,120,L"Arial",6,L"Arial",6,L"Nature 官方化学结构指南（Arial 6 pt）"},
        {L"RSC (1 Column)",12.2,0.5,1.6,1.25,1.8,20,120,L"Arial",7,L"Arial",7,L"RSC 官方参数；留白沿用模板；单栏/双栏结构样式相同"},
        {L"RSC (2 Column)",12.2,0.5,1.6,1.25,1.8,20,120,L"Arial",7,L"Arial",7,L"RSC 官方参数；留白沿用模板；单栏/双栏结构样式相同"},
        {L"Wiley",17.0078735,0.736999512,2.60786438,2.0125885,2.60786438,18,120,L"Arial",12,L"Arial",12,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"Adv. Synth. Catal.",17,0.75,2.6,2,2.6,18,120,L"Arial",12,L"Arial",12,L"Advanced Synthesis & Catalysis 官方作者指南"},
        {L"SYNTHESIS / SYNLETT",10.119685,0.566929134,1.38897638,0.907086614,1.75748031,18,120,L"Arial",6,L"Arial",6,L"Thieme SYNLETT 2026 / SYNTHESIS 2024 作者指南"},
        {L"Science of Synthesis",17.0078735,0.793685913,2.0125885,2.23936462,2.49447632,18,120,L"Arial",10,L"Arial",10,L"Thieme 推荐 Science of Synthesis 模板；数值对照本机内置样式"},
        {L"J. Chin. Chem. Soc.",14.4,0.6,2,1.6,2.5,18,120,L"Times New Roman",10,L"Times New Roman",10,L"Journal of the Chinese Chemical Society 官方作者指南"},
        {L"J. Het. Chem.",17,0.75,2.6,2,2.6,18,120,L"Arial",12,L"Arial",12,L"Journal of Heterocyclic Chemistry 官方作者指南"},
        {L"J. Mol. Mod. (1 Column)",14.3999939,0.599990845,2.0125885,1.58738708,2.49447632,18,120,L"Times New Roman",10,L"Times New Roman",10,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"J. Mol. Mod. (2 Column)",14.3999939,0.599990845,2.0125885,1.58738708,2.49447632,18,120,L"Times New Roman",10,L"Times New Roman",10,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
        {L"Verlag Helv. Chim. Acta",11.9,0.42,1.4,1.4,1.4,15,120,L"Arial",7,L"Arial",8.4,L"Helvetica Chimica Acta 官方作者指南；标签 7 pt / 文字 8.4 pt"},
        {L"Phytomedicine",20.0125885,0.992111206,1.33332825,1.24723816,3,8,120,L"Arial",12,L"Arial",12,L"ChemDraw 25.1 内置历史样式；请对照目标期刊现行要求"},
};return styles;}
inline const JournalStyle* journalStyle(const std::wstring& name){for(const auto& s:journalStyles())if(name==s.name)return &s;return nullptr;}
struct Style {
    double bondLength = 14.4, lineWidth = .6, boldWidth = 2, marginWidth = 1.6;
    double hashSpacing = 2.5, bondSpacing = 18, chainAngle = 120;
    std::wstring fontName = L"Arial", captionFontName = L"Arial";
    double fontSize = 10, captionFontSize = 10;
    std::wstring preset = L"ACS Document 1996";

    static bool parse(const wchar_t* text, double low, double high, double* value) {
        wchar_t* end = nullptr;
        const double parsed = std::wcstod(text, &end);
        if (end == text) return false;
        while (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n') ++end;
        if (*end || !std::isfinite(parsed) || parsed < low || parsed > high) return false;
        *value = parsed;
        return true;
    }

    static bool safe(const std::wstring& name, size_t maxLength) {
        return !name.empty() && name.size() <= maxLength && name.find_first_of(L"\r\n[]=") == std::wstring::npos
            && name.find(L'\0') == std::wstring::npos;
    }

    void use(const JournalStyle& style) {
        preset = style.name;
        bondLength = style.bondLength;
        lineWidth = style.lineWidth;
        boldWidth = style.boldWidth;
        marginWidth = style.marginWidth;
        hashSpacing = style.hashSpacing;
        bondSpacing = style.bondSpacing;
        chainAngle = style.chainAngle;
        fontName = style.font;
        fontSize = style.fontSize;
        captionFontName = style.captionFont;
        captionFontSize = style.captionSize;
    }
    void acsDefaults() { use(journalStyles().front()); }
    bool valid() const;

    template<typename ToUtf8>
    void apply(r62::core::Document& doc, ToUtf8 toUtf8) const {
        doc.noteTouchedDefaults();
        doc.noteTouchedExtensions(r62::core::ObjectRef(), true);
        doc.setDefaultBondLength(bondLength);
        doc.setDefaultLineWidth(lineWidth);
        doc.setDefaultFont(toUtf8(captionFontName));
        doc.setDefaultTextSize(captionFontSize);
        auto& ext = doc.documentExtensions();
        ext.set("studio.style.LabelSize", std::to_string(fontSize));
        ext.set("studio.style.LabelFontName", toUtf8(fontName));
        const std::pair<const char*, double> details[] = {
            {"ChainAngle", chainAngle}, {"BondSpacing", bondSpacing}, {"BoldWidth", boldWidth},
            {"MarginWidth", marginWidth}, {"HashSpacing", hashSpacing}
        };
        for (const auto& pair : details) {
            const auto value = std::to_string(pair.second);
            ext.set(std::string("studio.style.") + pair.first, value);
            ext.set(std::string("pst.attr.") + pair.first, value);
        }
    }
};

struct Field {
    const char* key;
    const char* label;
    double Style::*member;
    double low, high;
};

inline constexpr std::array<Field, 9> fields{{
    {"BondLength", "键长（pt）", &Style::bondLength, 2, 200},
    {"LineWidth", "线宽（pt）", &Style::lineWidth, .1, 12},
    {"BoldWidth", "粗键宽（pt）", &Style::boldWidth, .1, 24},
    {"MarginWidth", "留白（pt）", &Style::marginWidth, 0, 20},
    {"HashSpacing", "虚楔线间距（pt）", &Style::hashSpacing, .1, 20},
    {"BondSpacing", "键间距（%）", &Style::bondSpacing, 1, 100},
    {"ChainAngle", "链角（°）", &Style::chainAngle, 30, 180},
    {"FontSize", "原子字号（pt）", &Style::fontSize, 4, 96},
    {"CaptionFontSize", "文字字号（pt）", &Style::captionFontSize, 4, 96}
}};

inline bool Style::valid() const {
    for (const auto& field : fields) {
        const auto v = this->*field.member;
        if (!std::isfinite(v) || v < field.low || v > field.high) return false;
    }
    return safe(fontName, 120) && safe(captionFontName, 120) && safe(preset, 80);
}
}
