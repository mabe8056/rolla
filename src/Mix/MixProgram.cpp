#include "MixProgram.h"

#include <juce_core/juce_core.h>

#include <cstring>

namespace rolla
{
namespace
{

const char* kLaneNames[kMixLanes] = {
    "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX",
    "KICK", "SNARE", "CLAP", "RIM", "CH", "OH", "RIDE", "PERC",
};

const char* kDialects[kMixDialects] = { "UKMIN", "LONG", "POP", "GASKIN" };

const char* kMelodic[kMixMelodic] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX" };

float num (const juce::var& o, const char* key, float fallback) noexcept
{
    if (auto* d = o.getDynamicObject())
    {
        const auto v = d->getProperty (key);
        if (v.isDouble() || v.isInt() || v.isInt64())
            return (float) (double) v;
    }
    return fallback;
}

int numI (const juce::var& o, const char* key, int fallback) noexcept
{
    return (int) std::lround (num (o, key, (float) fallback));
}

MixSatType satType (const juce::String& s) noexcept
{
    if (s.containsIgnoreCase ("tape"))
        return MixSatType::Tape;
    if (s.containsIgnoreCase ("tube"))
        return MixSatType::Tube;
    if (s.containsIgnoreCase ("soft") || s.containsIgnoreCase ("tanh"))
        return MixSatType::Softclip;
    return MixSatType::None;
}

MixEqType eqType (const juce::String& s) noexcept
{
    if (s.containsIgnoreCase ("low"))
        return MixEqType::LowShelf;
    if (s.containsIgnoreCase ("high"))
        return MixEqType::HighShelf;
    if (s.containsIgnoreCase ("bell") || s.containsIgnoreCase ("peak"))
        return MixEqType::Bell;
    return MixEqType::Off;
}

void readEq (MixChain& c, const juce::var& eq) noexcept
{
    c.nEq = 0;
    if (! eq.isArray())
        return;
    for (const auto& item : *eq.getArray())
    {
        if (c.nEq >= 2 || ! item.isObject())
            break;
        auto& b = c.eq[c.nEq];
        b.type = eqType (item.getProperty ("type", {}).toString());
        b.freqHz = num (item, "freq_hz", 1000.f);
        b.gainDb = num (item, "gain_db", 0.f);
        b.q = num (item, "q", 0.7f);
        if (b.type != MixEqType::Off)
            ++c.nEq;
    }
}

void readChain (MixChain& c, const juce::var& lane) noexcept
{
    c = MixChain {};
    if (! lane.isObject())
        return;
    const auto hpf = lane.getProperty ("hpf", {});
    if (hpf.isObject())
    {
        c.hpfHz = num (hpf, "freq_hz", 20.f);
        c.hpfSlope = numI (hpf, "slope_db_oct", 12);
    }
    const auto lpf = lane.getProperty ("lpf", {});
    if (lpf.isObject())
    {
        c.lpfOn = true;
        c.lpfHz = num (lpf, "freq_hz", 20000.f);
        c.lpfSlope = numI (lpf, "slope_db_oct", 12);
    }
    readEq (c, lane.getProperty ("eq", {}));
    const auto comp = lane.getProperty ("comp", {});
    if (comp.isObject())
    {
        c.compThresholdDb = num (comp, "threshold_dbfs", 0.f);
        c.compRatio = num (comp, "ratio", 1.f);
        c.compAttackMs = num (comp, "attack_ms", 10.f);
        c.compReleaseMs = num (comp, "release_ms", 100.f);
        c.compKneeDb = num (comp, "knee_db", 0.f);
        c.compTargetGrDb = num (comp, "target_gr_db", 0.f);
    }
    const auto sat = lane.getProperty ("sat", {});
    if (sat.isObject())
    {
        c.sat = satType (sat.getProperty ("type", {}).toString());
        c.satDriveDb = num (sat, "drive_db", 0.f);
        c.satMix = std::clamp (num (sat, "mix_pct", 0.f) / 100.f, 0.f, 1.f);
    }
    const auto tilt = lane.getProperty ("tilt", {});
    if (tilt.isObject())
    {
        c.tiltDbPerOct = num (tilt, "db_per_oct", 0.f);
        c.tiltPivotHz = num (tilt, "pivot_hz", 1000.f);
    }
    const auto width = lane.getProperty ("width", {});
    if (width.isObject())
    {
        c.monoBelowHz = num (width, "mono_below_hz", 0.f);
        if (width.getDynamicObject() != nullptr && width.getDynamicObject()->hasProperty ("side_mid_max_db"))
        {
            const float mx = num (width, "side_mid_max_db", 0.f);
            if (mx <= -18.f)
                c.forceMono = true;
        }
    }
    c.outputTrimDb = num (lane, "output_trim_db", 0.f);
    c.stTargetLufs = num (lane, "st_target_lufs", -24.f);
    c.valid = true;
}

bool readOverride (MixOverride& o, const juce::var& src) noexcept
{
    o = MixOverride {};
    if (! src.isObject())
        return false;
    auto* d = src.getDynamicObject();
    auto take = [&] (const char* key, bool& flag, float& dest)
    {
        if (! d->hasProperty (key))
            return;
        const auto v = d->getProperty (key);
        if (! (v.isDouble() || v.isInt() || v.isInt64()))
            return;
        flag = true;
        dest = (float) (double) v;
    };
    take ("hpf.freq_hz", o.hasHpfHz, o.hpfHz);
    float slope = 0.f;
    take ("hpf.slope_db_oct", o.hasHpfSlope, slope);
    if (o.hasHpfSlope)
        o.hpfSlope = (int) std::lround (slope);
    take ("lpf.freq_hz", o.hasLpfHz, o.lpfHz);
    slope = 0.f;
    take ("lpf.slope_db_oct", o.hasLpfSlope, slope);
    if (o.hasLpfSlope)
        o.lpfSlope = (int) std::lround (slope);
    for (int i = 0; i < 2; ++i)
    {
        const auto f = juce::String ("eq[") + juce::String (i) + "].freq_hz";
        const auto g = juce::String ("eq[") + juce::String (i) + "].gain_db";
        const auto q = juce::String ("eq[") + juce::String (i) + "].q";
        take (f.toRawUTF8(), o.hasEqFreq[i], o.eqFreq[i]);
        take (g.toRawUTF8(), o.hasEqGain[i], o.eqGain[i]);
        take (q.toRawUTF8(), o.hasEqQ[i], o.eqQ[i]);
    }
    take ("comp.attack_ms", o.hasCompAttack, o.compAttackMs);
    take ("comp.release_ms", o.hasCompRelease, o.compReleaseMs);
    take ("comp.ratio", o.hasCompRatio, o.compRatio);
    take ("comp.knee_db", o.hasCompKnee, o.compKneeDb);
    take ("comp.target_gr_db", o.hasCompGr, o.compGr);
    if (d->hasProperty ("sat.type"))
    {
        o.hasSatType = true;
        o.sat = satType (d->getProperty ("sat.type").toString());
    }
    take ("sat.drive_db", o.hasSatDrive, o.satDriveDb);
    take ("sat.mix_pct", o.hasSatMix, o.satMix);
    if (o.hasSatMix)
        o.satMix = std::clamp (o.satMix / 100.f, 0.f, 1.f);
    take ("width.mono_below_hz", o.hasMonoBelow, o.monoBelowHz);
    o.used = true;
    return true;
}

} // namespace

const char* mixLaneName (int lane) noexcept
{
    if (lane < 0 || lane >= kMixLanes)
        return "";
    return kLaneNames[lane];
}

int mixLaneIndex (const char* name) noexcept
{
    if (name == nullptr)
        return -1;
    for (int i = 0; i < kMixLanes; ++i)
        if (std::strcmp (kLaneNames[i], name) == 0)
            return i;
    return -1;
}

void MixProgram::applyOverride (MixChain& dst, int part, const char* presetId) const
{
    if (part < 0 || part >= kMixMelodic || presetId == nullptr || presetId[0] == 0)
        return;
    const juce::String id (presetId);
    for (int i = 0; i < overrideCount[part]; ++i)
    {
        if (! id.equalsIgnoreCase (overrideId[part][i]))
            continue;
        const auto& o = overrideChain[part][i];
        const bool lockLow = part == 0;
        if (o.hasHpfHz && ! lockLow)
            dst.hpfHz = o.hpfHz;
        if (o.hasHpfSlope)
            dst.hpfSlope = o.hpfSlope;
        if (o.hasLpfHz)
        {
            dst.lpfOn = true;
            dst.lpfHz = o.lpfHz;
        }
        if (o.hasLpfSlope)
            dst.lpfSlope = o.lpfSlope;
        for (int b = 0; b < 2; ++b)
        {
            if (b >= dst.nEq)
                break;
            if (o.hasEqFreq[b])
                dst.eq[b].freqHz = o.eqFreq[b];
            if (o.hasEqGain[b])
                dst.eq[b].gainDb = o.eqGain[b];
            if (o.hasEqQ[b])
                dst.eq[b].q = o.eqQ[b];
        }
        if (o.hasCompAttack)
            dst.compAttackMs = o.compAttackMs;
        if (o.hasCompRelease)
            dst.compReleaseMs = o.compReleaseMs;
        if (o.hasCompRatio)
            dst.compRatio = o.compRatio;
        if (o.hasCompKnee)
            dst.compKneeDb = o.compKneeDb;
        if (o.hasCompGr)
            dst.compTargetGrDb = o.compGr;
        if (o.hasSatType)
            dst.sat = o.sat;
        if (o.hasSatDrive)
            dst.satDriveDb = o.satDriveDb;
        if (o.hasSatMix)
            dst.satMix = o.satMix;
        if (o.hasMonoBelow && ! lockLow)
            dst.monoBelowHz = o.monoBelowHz;
        return;
    }
}

bool MixProgram::loadFromMemory (const char* profilesJson, int profilesBytes,
                                 const char* trimJson, int trimBytes,
                                 const char* makeupJson, int makeupBytes)
{
    *this = MixProgram {};
    for (int d = 0; d < kMixDialects; ++d)
        for (int lane = 0; lane < kMixLanes; ++lane)
            tpCeilDb[d][lane] = 24.f;
    if (profilesJson == nullptr || profilesBytes <= 0)
        return false;
    const auto root = juce::JSON::parse (juce::String::fromUTF8 (profilesJson, profilesBytes));
    if (! root.isObject())
        return false;
    const auto dialects = root.getProperty ("dialects", {});
    if (! dialects.isObject())
        return false;

    for (int d = 0; d < kMixDialects; ++d)
    {
        const auto dialect = dialects.getProperty (kDialects[d], {});
        if (! dialect.isObject())
            continue;
        dialectTrimDb[d] = num (dialect, "output_trim_db", 0.f);
        const auto lanes = dialect.getProperty ("lanes", {});
        if (! lanes.isObject())
            continue;
        for (int part = 0; part < kMixMelodic; ++part)
            readChain (chain[d][part], lanes.getProperty (kMelodic[part], {}));
        const auto drums = lanes.getProperty ("DRUMS", {});
        const auto voices = drums.getProperty ("voices", {});
        if (voices.isObject())
        {
            for (int lane = kMixMelodic; lane < kMixLanes; ++lane)
                readChain (chain[d][lane], voices.getProperty (kLaneNames[lane], {}));
        }
    }

    const auto overs = root.getProperty ("preset_overrides", {}).getProperty ("presets", {});
    if (overs.isObject())
    {
        for (auto& prop : overs.getDynamicObject()->getProperties())
        {
            const auto key = prop.name.toString();
            const auto partName = key.upToFirstOccurrenceOf (":", false, false);
            const auto preset = key.fromFirstOccurrenceOf (":", false, false);
            int part = -1;
            for (int i = 0; i < kMixMelodic; ++i)
                if (partName.equalsIgnoreCase (kMelodic[i]))
                    part = i;
            if (part < 0 || overrideCount[part] >= kMixOverrideSlots)
                continue;
            const int slot = overrideCount[part]++;
            preset.copyToUTF8 (overrideId[part][slot], (int) sizeof overrideId[part][slot]);
            readOverride (overrideChain[part][slot], prop.value);
        }
    }

    if (trimJson != nullptr && trimBytes > 0)
    {
        const auto trim = juce::JSON::parse (juce::String::fromUTF8 (trimJson, trimBytes));
        const auto td = trim.getProperty ("dialects", {});
        for (int d = 0; d < kMixDialects; ++d)
        {
            const auto by = td.getProperty (kDialects[d], {}).getProperty ("by_mask", {});
            if (! by.isObject())
                continue;
            for (int mask = 0; mask < 64; ++mask)
            {
                const auto row = by.getProperty (juce::String (mask), {});
                sumTrimDb[d][mask] = num (row, "trim_db", 0.f);
            }
        }
    }

    if (makeupJson != nullptr && makeupBytes > 0)
    {
        const auto mk = juce::JSON::parse (juce::String::fromUTF8 (makeupJson, makeupBytes));
        const auto md = mk.getProperty ("dialects", {});
        for (int d = 0; d < kMixDialects; ++d)
        {
            const auto row = md.getProperty (kDialects[d], {});
            sumAlignDb[d] = num (row, "sum_align_db", 0.f);
            const auto ceils = row.getProperty ("tp_ceil_db", {});
            if (auto* ceilObj = ceils.getDynamicObject())
            {
                for (int lane = 0; lane < kMixLanes; ++lane)
                    if (ceilObj->hasProperty (kLaneNames[lane]))
                        tpCeilDb[d][lane] = num (ceils, kLaneNames[lane], 24.f);
            }
            const auto lanes = row.getProperty ("lanes", {});
            if (! lanes.isObject())
                continue;
            for (int lane = 0; lane < kMixLanes; ++lane)
                makeupDb[d][lane] = num (lanes, kLaneNames[lane], 0.f);
        }
    }

    loaded = chain[0][0].valid && chain[0][6].valid;
    return loaded;
}

} // namespace rolla
