/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/system/exception/validation.h"
#include "reone/texgen/job.h"

using namespace reone;
using namespace reone::texgen;

namespace po = boost::program_options;

static const char *kUsage =
    "texgen: make normal and material maps from your own KotOR textures.\n"
    "\n"
    "  texgen --game <install dir | key | erf | rim | tga | tpc> --out <folder> [options]\n"
    "\n"
    "The output (<name>_n.dds, <name>_m.dds - or .tga with --format tga - and texgen_manifest.json) is derived from game\n"
    "files: keep it on your machine, never share or commit it.\n";

static int run(const po::variables_map &args) {
    if (args.count("print-default-rules")) {
        std::cout << boost::json::serialize(Rules::defaults().toJson()) << std::endl;
        return 0;
    }
    if (!args.count("game") || !args.count("out")) {
        std::cerr << kUsage << "\nneeds --game and --out (see --help)\n";
        return 2;
    }

    Rules rules = args.count("rules") ? Rules::load(args["rules"].as<std::string>()) : Rules::defaults();
    SourceSet sources = scanSources(args["game"].as<std::string>());
    for (const auto &warning : sources.warnings) {
        std::cerr << "texgen: warning: " << warning << "\n";
    }

    JobOptions options;
    options.outputDir = args["out"].as<std::string>();
    options.baseStrength = args["strength"].as<float>();
    options.threads = args["threads"].as<int>();
    options.force = args.count("force") > 0;
    options.dryRun = args.count("dry-run") > 0;
    auto format = args["format"].as<std::string>();
    if (format == "dds") {
        options.format = OutputFormat::Dds;
    } else if (format == "tga") {
        options.format = OutputFormat::Tga;
    } else {
        std::cerr << "texgen: --format must be dds or tga\n";
        return 2;
    }
    auto codec = args["material-codec"].as<std::string>();
    if (codec == "bc7") {
        options.materialCodec = MaterialCodec::BC7;
    } else if (codec == "bc1") {
        options.materialCodec = MaterialCodec::BC1;
    } else {
        std::cerr << "texgen: --material-codec must be bc7 or bc1\n";
        return 2;
    }
    if (args.count("only")) {
        options.only = args["only"].as<std::string>();
    }
    if (options.baseStrength < 0.0f) {
        std::cerr << "texgen: --strength cannot be negative\n";
        return 2;
    }
    bool quiet = args.count("quiet") > 0;

    std::cout << "texgen: " << sources.textures.size() << " textures found" << std::endl;
    static const char *kNames[] = {"generated", "skipped", "unchanged", "FAILED"};
    auto summary = runJob(sources, rules, options, [&](const Progress &p) {
        if (quiet && p.outcome != Outcome::Failed) {
            return;
        }
        // One line per texture, easy for a launcher to read: progress <done>/<total> <name> <outcome> [detail]
        std::cout << "progress " << p.done << "/" << p.total << " " << p.texture << " "
                  << kNames[static_cast<int>(p.outcome)];
        if (!p.detail.empty()) {
            std::cout << " " << p.detail;
        }
        std::cout << std::endl;
    });
    std::cout << "texgen: done: " << summary.generated << " generated, " << summary.unchanged << " unchanged, "
              << summary.skipped << " skipped, " << summary.failed << " failed (of " << summary.total << ")" << std::endl;
    return summary.failed > 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    try {
        po::options_description description("Options");
        description.add_options()
            ("help,h", "this help")
            ("game,g", po::value<std::string>(), "a KotOR install directory, or one KEY, ERF, MOD, RIM, TGA or TPC file")
            ("out,o", po::value<std::string>(), "folder for the generated maps and the manifest")
            ("rules,r", po::value<std::string>(), "a JSON rules file (default: built-in naming-convention rules)")
            ("strength,s", po::value<float>()->default_value(2.0f), "base normal map strength")
            ("threads,j", po::value<int>()->default_value(0), "worker threads (0: one per core)")
            ("format", po::value<std::string>()->default_value("dds"), "output format: dds (BC5 normals, BC7 materials, mip maps; about a quarter of the size) or tga")
            ("material-codec", po::value<std::string>()->default_value("bc7"), "DDS material maps: bc7 or bc1 (half the size, coarser)")
            ("only", po::value<std::string>(), "only textures whose name matches this glob (* and ?)")
            ("force", "regenerate even what the manifest says is up to date")
            ("dry-run", "classify and report; write nothing")
            ("quiet,q", "print only failures and the summary")
            ("print-default-rules", "print the built-in rules as JSON and exit");
        po::variables_map args;
        po::store(po::command_line_parser(argc, argv).options(description).run(), args);
        if (args.count("help")) {
            std::cout << kUsage << "\n" << description << std::endl;
            return 0;
        }
        po::notify(args);
        return run(args);
    } catch (const std::exception &e) {
        std::cerr << "texgen: " << e.what() << std::endl;
        return 1;
    }
}
