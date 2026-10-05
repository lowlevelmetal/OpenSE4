#include "mods/data_set.hpp"

#include "game/ai_data.hpp"
#include "mods/apply.hpp"
#include "ruleset/ability_names.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

namespace opense4::mods {

namespace fs = std::filesystem;
using datafile::DataFile;
using datafile::Field;
using datafile::keysEqual;
using datafile::Record;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return parts;
}

std::string fileName(std::string_view path) {
    const size_t slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// Whether an AI file is one of those a patch's `files` names.
bool chosenFile(const std::string& path, const std::vector<std::string>& files) {
    if (files.empty()) return true;
    const std::vector<std::string> p = split(ruleset::indexKey(path));
    for (const std::string& spec : files) {
        if (spec == "default" && p.size() == 2 && p[0] == "ai" && p[1].starts_with("default_")) return true;
        if (spec.starts_with("style:") && p.size() == 3 && p[0] == "ai" && p[1] == lower(spec.substr(6))) return true;
        if (spec.starts_with("race:") && p.size() == 4 && p[0] == "pictures" && (p[1] == "races" || p[1] == "raceneutral") && p[2] == lower(spec.substr(5)))
            return true;
    }
    return false;
}

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

std::optional<std::string> aiTableAt(std::string_view relative) {
    const std::vector<std::string> p = split(ruleset::indexKey(relative));
    const bool where = (p.size() == 2 && p[0] == "ai") || (p.size() == 3 && p[0] == "ai") ||
                       (p.size() == 4 && p[0] == "pictures" && (p[1] == "races" || p[1] == "raceneutral"));
    if (!where) return std::nullopt;
    return aiTableOfFileName(p.back());
}

std::shared_ptr<GameData> GameData::build(fs::path gameRoot, fs::path dataDir, ModSet mods, const LoadOptions& options, datafile::Diagnostics& diag) {
    std::shared_ptr<GameData> d(new GameData);
    d->root_ = std::move(gameRoot);
    d->dataDir_ = dataDir.empty() && !d->root_.empty() ? ruleset::childIgnoringCase(d->root_, "Data") : std::move(dataDir);
    d->mods_ = std::move(mods);
    d->install_ = ruleset::FileIndex::ofGameFiles(d->root_);
    for (const Package& p : d->mods_.packages) {
        Layer layer{p.id(), {}};
        for (const GameMount& m : p.gameFiles()) layer.index.add(m.installPath, m.real);
        d->layers_.push_back(std::move(layer));
    }
    std::string records;
    for (const ruleset::ModRecord& r : d->mods_.records()) records += std::format("{}={}:{};", r.id, r.version, r.hash);
    d->key_ = std::format("mods|{}|{}|{}", d->root_.generic_string(), d->dataDir_.generic_string(), records);

    std::vector<std::string> errors;
    d->loadBase(errors);
    d->applyMods(options, errors);
    d->checkReferences(errors);
    diag.errors.insert(diag.errors.end(), errors.begin(), errors.end());
    return d;
}

void GameData::learn(const TableSpec& table, const DataFile& file) {
    Known& k = known_[std::string(table.name)];
    for (const Record& r : file.records)
        for (const Field& f : r.fields) {
            k.keys.insert(datafile::normalizeKey(f.key));
            k.patterns.insert(datafile::keyPattern(f.key));
        }
}

void GameData::loadBase(std::vector<std::string>&) {
    // The data folder as the loader would read it.
    for (const ruleset::DataFileName& n : ruleset::dataFileNames()) {
        auto f = datafile::load(ruleset::childIgnoringCase(dataDir_, n.name));
        if (!f) continue;  // the loader reports a missing file
        if (const TableSpec* t = tableOfDataFile(n.name)) learn(*t, *f);
        data_.emplace(lower(n.name), std::move(*f));
    }
    // The computer players' tables.
    for (const auto& [written, real] : install_.files()) {
        const auto table = aiTableAt(written);
        if (!table) continue;
        auto f = datafile::load(real);
        if (!f) continue;
        if (const TableSpec* t = tableOfAiFile(*table)) learn(*t, *f);
        ai_.emplace(ruleset::indexKey(written), AiFile{written, *table, std::move(*f)});
    }
}

void GameData::applyMods(const LoadOptions& options, std::vector<std::string>& errors) {
    const std::shared_ptr<GeneratorRunner> runner = options.generators ? options.generators : defaultGeneratorRunner();
    for (const Package& p : mods_.packages) {
        // Its files take the place of the earlier ones.
        for (const GameMount& m : p.gameFiles()) {
            const std::string key = ruleset::indexKey(m.installPath);
            const std::string origin = std::format("mod {}: {}", p.id(), m.packagePath);
            if (key.starts_with("data/")) {
                auto f = datafile::load(m.real);
                if (!f) {
                    errors.push_back(std::format("{}: {}", origin, f.error()));
                    continue;
                }
                const std::string name = fileName(m.installPath);
                const TableSpec* t = tableOfDataFile(name);
                if (!t) continue;  // a data file the game does not read
                f->name = std::string(t->file);
                f->origin = origin;
                learn(*t, *f);
                data_[lower(name)] = std::move(*f);
            } else if (const auto table = aiTableAt(m.installPath)) {
                auto f = datafile::load(m.real);
                if (!f) {
                    errors.push_back(std::format("{}: {}", origin, f.error()));
                    continue;
                }
                f->origin = origin;
                if (const TableSpec* t = tableOfAiFile(*table)) learn(*t, *f);
                ai_[key] = AiFile{m.installPath, *table, std::move(*f)};
            }
        }
        // Then its patches and generators, by file name.
        PatchSet set;
        for (const PackageFile* f : p.dataScripts()) {
            Origin origin{p.id(), f->path, false};
            if (lower(f->path).ends_with(".toml")) {
                if (auto node = parsePatchToml(readAll(f->real), origin, errors)) parsePatch(*node, origin, set, errors);
                continue;
            }
            auto out = runner->run(GeneratorRequest{p.id(), f->path, readAll(f->real)});
            if (!out) {
                errors.push_back(std::format("mod {}: {}", p.id(), out.error()));
                continue;
            }
            origin.generated = true;
            parsePatch(nodeFromValue(*out), origin, set, errors);
        }
        declare(set, errors);
        apply(set, errors);
    }
}

DataFile* GameData::dataFileOf(const TableSpec& table, bool create) {
    auto it = data_.find(lower(table.file));
    if (it != data_.end()) return &it->second;
    if (!create) return nullptr;
    DataFile f;
    f.name = std::string(table.file);
    return &data_.emplace(lower(table.file), std::move(f)).first->second;
}

void GameData::declare(const PatchSet& set, std::vector<std::string>& errors) {
    for (const Declaration& d : set.abilities) {
        if (ruleset::abilityNameStatus(d.ability.name) != ruleset::AbilityNameStatus::Unknown) {
            errors.push_back(std::format("{}: '{}' is one of the game's own abilities: it needs no declaring", d.where, d.ability.name));
            continue;
        }
        const auto same = std::find_if(declared_.begin(), declared_.end(), [&](const ruleset::DeclaredAbility& a) { return keysEqual(a.name, d.ability.name); });
        if (same == declared_.end()) {
            declared_.push_back(d.ability);
        } else if (same->combine != d.ability.combine) {
            errors.push_back(std::format("{}: mod {} declared '{}' with combine = \"{}\"; this declaration says \"{}\"", d.where, same->mod, d.ability.name,
                                         ruleset::displayName(same->combine), ruleset::displayName(d.ability.combine)));
        }
    }
}

void GameData::apply(const PatchSet& set, std::vector<std::string>& errors) {
    for (const PatchOp& op : set.ops) {
        if (const auto* list = std::get_if<ListOp>(&op)) {
            DataFile* f = dataFileOf(*list->table, true);
            ApplyContext ctx{&errors, &removals_, nullptr, f->name};
            applyListOp(*f, *list, ctx);
            continue;
        }
        const RecordOp& r = std::get<RecordOp>(op);
        const TableSpec& t = *r.table;
        const Known& known = known_[std::string(t.name)];
        if (!t.ai) {
            DataFile* f = dataFileOf(t, false);
            if (!f) {
                errors.push_back(std::format("{}: the data folder has no {} to patch", r.where, t.file));
                continue;
            }
            ApplyContext ctx{&errors, &removals_, &known.patterns, f->name};
            applyRecordOp(*f, r, ctx);
            continue;
        }
        // A computer players' table: every file of it the operation names.
        size_t files = 0, applied = 0;
        for (auto& [key, file] : ai_) {
            if (!keysEqual(file.table, t.file) || !chosenFile(file.written, r.files)) continue;
            ++files;
            ApplyContext ctx{&errors, &removals_, &known.patterns, file.written};
            applied += applyRecordOp(file.file, r, ctx, false);
        }
        if (files == 0)
            errors.push_back(std::format("{}: no {} file{} (<prefix>_AI_{}.txt) to patch", r.where, t.name,
                                         r.files.empty() ? "" : " of those 'files' names", t.file));
        else if (applied == 0 && r.kind != OpKind::Add)
            errors.push_back(std::format("{}: no {} file has a record {} to {}", r.where, t.name, r.select.describe(),
                                         r.kind == OpKind::Change ? "change" : "remove"));
    }
}

void GameData::checkReferences(std::vector<std::string>& errors) {
    // The names each referred-to table still has, after every mod.
    auto exists = [&](const TableSpec& table, const std::string& name) {
        const DataFile* f = dataFileOf(table, false);
        if (!f) return false;
        for (const Record& r : f->records)
            if (const Field* k = r.find(table.keyField); k && keysEqual(k->value, name)) return true;
        return false;
    };
    // Every file holding references of a spec.
    auto filesOf = [&](const ReferenceSpec& ref) {
        std::vector<DataFile*> out;
        const TableSpec* from = findTable(ref.from);
        if (!from) return out;
        if (!from->ai) {
            if (DataFile* f = dataFileOf(*from, false)) out.push_back(f);
        } else {
            for (auto& [key, file] : ai_)
                if (keysEqual(file.table, from->file)) out.push_back(&file.file);
        }
        return out;
    };
    for (size_t i = 0; i < removals_.size(); ++i) {
        const Removal removal = removals_[i];  // a copy: cascades add removals
        if (removal.table->keyField.empty() || exists(*removal.table, removal.name)) continue;  // added again, or no name
        for (const ReferenceSpec& ref : references()) {
            if (ref.to != removal.table->name) continue;
            const TableSpec* from = findTable(ref.from);
            for (DataFile* file : filesOf(ref)) {
                for (size_t r = 0; r < file->records.size();) {
                    Record& record = file->records[r];
                    const Field* hit = nullptr;
                    for (const Field& f : record.fields)
                        if (matchNumbered(f.key, ref.keyFormat) && keysEqual(f.value, removal.name)) {
                            hit = &f;
                            break;
                        }
                    if (!hit) {
                        ++r;
                        continue;
                    }
                    const std::string where = describeField(*file, record, *hit);
                    if (!removal.cascade) {
                        errors.push_back(std::format("{}: removing {} '{}' leaves a reference to it in {} ('{}'): remove or change that too, or "
                                                     "remove with cascade = true",
                                                     removal.where, removal.table->name, removal.name, where, hit->key));
                        ++r;
                        continue;
                    }
                    switch (ref.cascade) {
                        case Cascade::RemoveRecord: {
                            if (from && !from->keyField.empty() && from->kind == TableKind::Records)
                                if (const Field* k = record.find(from->keyField)) removals_.push_back({from, k->value, removal.where, true});
                            file->records.erase(file->records.begin() + static_cast<std::ptrdiff_t>(r));
                            continue;  // the next record is at r now
                        }
                        case Cascade::RemoveEntry: {
                            const GroupSpec* group = from ? from->group(ref.group) : nullptr;
                            if (!group) break;
                            std::vector<GroupEntry> entries = readGroup(record, *group);
                            std::erase_if(entries, [&](const GroupEntry& e) {
                                return std::any_of(e.fields.begin(), e.fields.end(), [&](const Field& f) {
                                    return matchNumbered(f.key, ref.keyFormat) && keysEqual(f.value, removal.name);
                                });
                            });
                            writeGroup(record, *group, entries, removal.where);
                            break;
                        }
                        case Cascade::ClearField:
                            for (Field& f : record.fields)
                                if (matchNumbered(f.key, ref.keyFormat) && keysEqual(f.value, removal.name)) {
                                    f.value = std::string(ref.clearTo);
                                    f.raw = " " + f.value;
                                    f.origin = removal.where;
                                }
                            break;
                        case Cascade::Refuse:
                            errors.push_back(std::format("{}: removing {} '{}': {} ('{}') needs it, and cascade cannot change that: change that "
                                                         "record first",
                                                         removal.where, removal.table->name, removal.name, where, hit->key));
                            break;
                    }
                    ++r;
                }
            }
        }
    }
}

std::expected<DataFile, std::string> GameData::dataFile(std::string_view name) const {
    if (const auto it = data_.find(lower(name)); it != data_.end()) return it->second;
    if (const auto p = path("Data/" + std::string(name))) return datafile::load(*p);
    return datafile::load(ruleset::childIgnoringCase(dataDir_, name));
}

std::expected<DataFile, std::string> GameData::file(std::string_view relative) const {
    if (const auto it = ai_.find(ruleset::indexKey(relative)); it != ai_.end()) return it->second.file;
    const auto p = path(relative);
    if (!p) return std::unexpected(std::format("{}: no such file in the game folder or its mods", relative));
    auto f = datafile::load(*p);
    // A mod's copy says so.
    for (size_t i = layers_.size(); f && i-- > 0;)
        if (layers_[i].index.find(relative)) {
            f->origin = std::format("mod {}", layers_[i].name);
            break;
        }
    return f;
}

std::optional<fs::path> GameData::path(std::string_view relative) const {
    for (size_t i = layers_.size(); i-- > 0;)
        if (const fs::path* p = layers_[i].index.find(relative)) return *p;
    if (const fs::path* p = install_.find(relative)) return *p;
    return std::nullopt;
}

std::vector<ruleset::FileEntry> GameData::list(std::string_view relativeDir) const {
    std::map<std::string, ruleset::FileEntry> entries;
    install_.listInto(relativeDir, entries);
    for (const Layer& layer : layers_) layer.index.listInto(relativeDir, entries);
    return ruleset::sortedEntries(entries);
}

uint64_t GameData::fingerprint() const {
    std::call_once(fingerprintOnce_, [&] { fingerprint_ = ruleset::hashGameFiles(install_); });
    return fingerprint_;
}

std::vector<const DataFile*> GameData::dataFiles() const {
    std::vector<const DataFile*> out;
    for (const ruleset::DataFileName& n : ruleset::dataFileNames())
        if (const auto it = data_.find(lower(n.name)); it != data_.end()) out.push_back(&it->second);
    return out;
}

std::vector<std::pair<std::string, const DataFile*>> GameData::aiFiles() const {
    std::vector<std::pair<std::string, const DataFile*>> out;
    for (const auto& [key, file] : ai_) out.emplace_back(file.written, &file.file);
    return out;
}

void GameData::checkAfterLoad(const datafile::Diagnostics& load, datafile::Diagnostics& out) const {
    auto knownOf = [&](std::string_view table) -> const Known* {
        const auto it = known_.find(std::string(table));
        return it == known_.end() ? nullptr : &it->second;
    };
    auto inList = [](const TableSpec& t, std::string_view key) -> const GroupSpec* {
        for (const GroupSpec& g : t.groups)
            for (std::string_view item : g.items)
                if (matchNumbered(key, item)) return &g;
        return nullptr;
    };
    auto report = [&](const TableSpec& t, std::string_view where, std::string_view file, std::string_view record, std::string_view key) {
        if (const GroupSpec* g = inList(t, key))
            out.errors.push_back(std::format("{}: {} [{}]: '{}' is past the end of its list ({}), so nothing reads it: add entries with add = {{ {} = [...] }}",
                                             where, file, record, key, g->countKey.empty() ? "the numbers must run from 1" : g->countKey, g->name));
        else
            out.errors.push_back(std::format("{}: {} [{}]: {} records have no field '{}' (check the spelling)", where, file, record, t.name, key));
    };
    // The data folder: what the typed loaders left unread.
    for (const datafile::UnreadPatchedField& u : load.unreadPatched) {
        const TableSpec* t = tableOfDataFile(u.file);
        if (!t || t->openFields) continue;
        const Known* k = knownOf(t->name);
        if (!inList(*t, u.key) && k && k->keys.contains(datafile::normalizeKey(u.key))) continue;  // a field the files have that we do not read yet
        report(*t, u.origin, u.file, u.record, u.key);
    }
    // The computer players' tables: what their readers leave unread.
    for (const auto& [key, file] : ai_) {
        const TableSpec* t = tableOfAiFile(file.table);
        if (!t || t->openFields) continue;
        const bool patched = std::any_of(file.file.records.begin(), file.file.records.end(), [](const Record& r) {
            return std::any_of(r.fields.begin(), r.fields.end(), [](const Field& f) { return !f.origin.empty(); });
        });
        if (!patched) continue;
        const Known* k = knownOf(t->name);
        for (const Field* f : game::ai::unreadPatchedFields(file.table, file.file)) {
            if (!inList(*t, f->key) && k && k->keys.contains(datafile::normalizeKey(f->key))) continue;
            const Record* owner = nullptr;
            for (const Record& r : file.file.records)
                for (const Field& g : r.fields)
                    if (&g == f) owner = &r;
            report(*t, f->origin, file.written, owner ? recordLabel(*owner) : std::string{}, f->key);
        }
    }
}

LoadedDataSet loadDataSet(const fs::path& gameRoot, const fs::path& dataDir, const ModSet& mods, const LoadOptions& options) {
    LoadedDataSet out;
    std::shared_ptr<GameData> data = GameData::build(gameRoot, dataDir, mods, options, out.diagnostics);
    ruleset::LoadOptions lo;
    lo.declaredAbilities = data->declaredAbilities();
    ruleset::LoadResult loaded = ruleset::loadRuleset(data, lo);
    out.diagnostics.errors.insert(out.diagnostics.errors.end(), loaded.diagnostics.errors.begin(), loaded.diagnostics.errors.end());
    out.diagnostics.warnings.insert(out.diagnostics.warnings.end(), loaded.diagnostics.warnings.begin(), loaded.diagnostics.warnings.end());
    for (const auto& [k, n] : loaded.diagnostics.unreadFields) out.diagnostics.unreadFields[k] += n;
    out.diagnostics.unreadPatched = loaded.diagnostics.unreadPatched;
    data->checkAfterLoad(loaded.diagnostics, out.diagnostics);
    if (loaded.ruleset) {
        loaded.ruleset->mods = data->mods().records();
        out.ruleset = std::move(loaded.ruleset);
    }
    out.data = std::move(data);
    return out;
}

} // namespace opense4::mods
