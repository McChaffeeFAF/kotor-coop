# Copyright (c) 2020-2023 The reone project contributors

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

if(MSVC)
    find_package(GTest CONFIG REQUIRED)
else()
    find_package(GTest REQUIRED)
endif()

set(TESTS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/test)

set(TESTS_HEADERS
    ${TESTS_SOURCE_DIR}/checkutil.h
    ${TESTS_SOURCE_DIR}/fixtures/archive.h
    ${TESTS_SOURCE_DIR}/fixtures/audio.h
    ${TESTS_SOURCE_DIR}/fixtures/data.h
    ${TESTS_SOURCE_DIR}/fixtures/engine.h
    ${TESTS_SOURCE_DIR}/fixtures/game.h
    ${TESTS_SOURCE_DIR}/fixtures/graphics.h
    ${TESTS_SOURCE_DIR}/fixtures/gui.h
    ${TESTS_SOURCE_DIR}/fixtures/itempresentation.h
    ${TESTS_SOURCE_DIR}/fixtures/movie.h
    ${TESTS_SOURCE_DIR}/fixtures/resource.h
    ${TESTS_SOURCE_DIR}/fixtures/scene.h
    ${TESTS_SOURCE_DIR}/fixtures/script.h
    ${TESTS_SOURCE_DIR}/fixtures/system.h
    ${TESTS_SOURCE_DIR}/support/nsscomp.h)

set(TESTS_SOURCES
    ${TESTS_SOURCE_DIR}/audio/mixer.cpp
    ${TESTS_SOURCE_DIR}/audio/format/wavreader.cpp
    ${TESTS_SOURCE_DIR}/engine/controlprotocol.cpp
    ${TESTS_SOURCE_DIR}/engine/graphicsoptions.cpp
    ${TESTS_SOURCE_DIR}/extract/capsule.cpp
    ${TESTS_SOURCE_DIR}/extract/chitin.cpp
    ${TESTS_SOURCE_DIR}/extract/fileresource.cpp
    ${TESTS_SOURCE_DIR}/extract/finder.cpp
    ${TESTS_SOURCE_DIR}/extract/installation.cpp
    ${TESTS_SOURCE_DIR}/extract/modulealignment.cpp
    ${TESTS_SOURCE_DIR}/fixtures/engine.cpp
    ${TESTS_SOURCE_DIR}/support/nsscomp.cpp
    ${TESTS_SOURCE_DIR}/script/nsscomp.cpp
    ${TESTS_SOURCE_DIR}/texgen/texgen.cpp
    ${TESTS_SOURCE_DIR}/game/action.cpp
    ${TESTS_SOURCE_DIR}/game/charactermenu.cpp
    ${TESTS_SOURCE_DIR}/game/coopnet.cpp
    ${TESTS_SOURCE_DIR}/game/cooploot.cpp
    ${TESTS_SOURCE_DIR}/game/coopprompts.cpp
    ${TESTS_SOURCE_DIR}/net/protocol.cpp
    ${TESTS_SOURCE_DIR}/net/roundtrip.cpp
    ${TESTS_SOURCE_DIR}/game/actionactor.cpp
    ${TESTS_SOURCE_DIR}/game/area.cpp
    ${TESTS_SOURCE_DIR}/game/c10movement.cpp
    ${TESTS_SOURCE_DIR}/game/c12audiocine.cpp
    ${TESTS_SOURCE_DIR}/game/p2cine.cpp
    ${TESTS_SOURCE_DIR}/game/p2ui.cpp
    ${TESTS_SOURCE_DIR}/game/k2reconcile.cpp
    ${TESTS_SOURCE_DIR}/game/assigncommand.cpp
    ${TESTS_SOURCE_DIR}/game/d20/class.cpp
    ${TESTS_SOURCE_DIR}/game/determinism.cpp
    ${TESTS_SOURCE_DIR}/game/d20/spells.cpp
    ${TESTS_SOURCE_DIR}/game/c01rules.cpp
    ${TESTS_SOURCE_DIR}/game/conversation.cpp
    ${TESTS_SOURCE_DIR}/game/computer.cpp
    ${TESTS_SOURCE_DIR}/game/globalnumber.cpp
    ${TESTS_SOURCE_DIR}/game/mainmenu.cpp
    ${TESTS_SOURCE_DIR}/game/globalfade.cpp
    ${TESTS_SOURCE_DIR}/game/p1ui.cpp
    ${TESTS_SOURCE_DIR}/game/p2timing.cpp
    ${TESTS_SOURCE_DIR}/game/p2combat.cpp
    ${TESTS_SOURCE_DIR}/game/p1k2menus.cpp
    ${TESTS_SOURCE_DIR}/game/p3menus.cpp
    ${TESTS_SOURCE_DIR}/game/interaction.cpp
    ${TESTS_SOURCE_DIR}/game/equipmentoperation.cpp
    ${TESTS_SOURCE_DIR}/game/hudtags.cpp
    ${TESTS_SOURCE_DIR}/game/k1hud.cpp
    ${TESTS_SOURCE_DIR}/game/weaponsets.cpp
    ${TESTS_SOURCE_DIR}/game/effect.cpp
    ${TESTS_SOURCE_DIR}/game/visualeffects.cpp
    ${TESTS_SOURCE_DIR}/game/entrylifecycle.cpp
    ${TESTS_SOURCE_DIR}/game/fixedtick.cpp
    ${TESTS_SOURCE_DIR}/game/galaxymap.cpp
    ${TESTS_SOURCE_DIR}/game/gateg2.cpp
    ${TESTS_SOURCE_DIR}/game/c02events.cpp
    ${TESTS_SOURCE_DIR}/game/conversationleader.cpp
    ${TESTS_SOURCE_DIR}/game/c03stats.cpp
    ${TESTS_SOURCE_DIR}/game/k2merge.cpp
    ${TESTS_SOURCE_DIR}/game/k2prestige.cpp
    ${TESTS_SOURCE_DIR}/game/guiscreens.cpp
    ${TESTS_SOURCE_DIR}/game/c04audio.cpp
    ${TESTS_SOURCE_DIR}/game/c08effects.cpp
    ${TESTS_SOURCE_DIR}/game/c09aicombat.cpp
    ${TESTS_SOURCE_DIR}/game/c11data.cpp
    ${TESTS_SOURCE_DIR}/game/c11bdata.cpp
    ${TESTS_SOURCE_DIR}/game/p1game.cpp
    ${TESTS_SOURCE_DIR}/game/p1combat.cpp
    ${TESTS_SOURCE_DIR}/game/c08beffects.cpp
    ${TESTS_SOURCE_DIR}/game/galaxymaproutines.cpp
    ${TESTS_SOURCE_DIR}/game/partycontrol.cpp
    ${TESTS_SOURCE_DIR}/game/galaxymapstate.cpp
    ${TESTS_SOURCE_DIR}/game/worldtime.cpp
    ${TESTS_SOURCE_DIR}/game/savedruntime.cpp
    ${TESTS_SOURCE_DIR}/game/saveprovenance.cpp
    ${TESTS_SOURCE_DIR}/game/savedsituation.cpp
    ${TESTS_SOURCE_DIR}/game/journal.cpp
    ${TESTS_SOURCE_DIR}/game/messagebus.cpp
    ${TESTS_SOURCE_DIR}/game/minigame.cpp
    ${TESTS_SOURCE_DIR}/game/minigamesession.cpp
    ${TESTS_SOURCE_DIR}/game/modulename.cpp
    ${TESTS_SOURCE_DIR}/game/modulesnapshot.cpp
    ${TESTS_SOURCE_DIR}/game/loadfromsavegame.cpp
    ${TESTS_SOURCE_DIR}/game/savenpcstate.cpp
    ${TESTS_SOURCE_DIR}/game/savegame.cpp
    ${TESTS_SOURCE_DIR}/game/savedgame.cpp
    ${TESTS_SOURCE_DIR}/game/saveload.cpp
    ${TESTS_SOURCE_DIR}/game/saveslotpublisher.cpp
    ${TESTS_SOURCE_DIR}/game/savewidesnapshot.cpp
    ${TESTS_SOURCE_DIR}/game/object.cpp
    ${TESTS_SOURCE_DIR}/game/runtimesession.cpp
    ${TESTS_SOURCE_DIR}/game/pazaak.cpp
    ${TESTS_SOURCE_DIR}/game/pazaakintegration.cpp
    ${TESTS_SOURCE_DIR}/game/pazaaksession.cpp
    ${TESTS_SOURCE_DIR}/game/presentationpointer.cpp
    ${TESTS_SOURCE_DIR}/game/partyroutines.cpp
    ${TESTS_SOURCE_DIR}/game/gateg1.cpp
    ${TESTS_SOURCE_DIR}/game/talents.cpp
    ${TESTS_SOURCE_DIR}/game/companionbrain.cpp
    ${TESTS_SOURCE_DIR}/game/pathfinder.cpp
    ${TESTS_SOURCE_DIR}/game/regeneration.cpp
    ${TESTS_SOURCE_DIR}/game/routines1.cpp
    ${TESTS_SOURCE_DIR}/game/routines2.cpp
    ${TESTS_SOURCE_DIR}/game/routines3.cpp
    ${TESTS_SOURCE_DIR}/game/nssscripts.cpp
    ${TESTS_SOURCE_DIR}/game/rulesaudit.cpp
    ${TESTS_SOURCE_DIR}/game/routines4.cpp
    ${TESTS_SOURCE_DIR}/game/reputes.cpp
    ${TESTS_SOURCE_DIR}/game/rostermaterialization.cpp
    ${TESTS_SOURCE_DIR}/game/itemmenubacking.cpp
    ${TESTS_SOURCE_DIR}/game/sharedpresentation.cpp
    ${TESTS_SOURCE_DIR}/game/spitempresentation.cpp
    ${TESTS_SOURCE_DIR}/game/statussummary.cpp
    ${TESTS_SOURCE_DIR}/game/statedump.cpp
    ${TESTS_SOURCE_DIR}/game/stringroutines.cpp
    ${TESTS_SOURCE_DIR}/game/timingepoch.cpp
    ${TESTS_SOURCE_DIR}/game/transitioncandidate.cpp
    ${TESTS_SOURCE_DIR}/game/transitions.cpp
    ${TESTS_SOURCE_DIR}/game/turret.cpp
    ${TESTS_SOURCE_DIR}/graphics/aabb.cpp
    ${TESTS_SOURCE_DIR}/graphics/context.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/bwmreader.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/malformed.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/mdlemitter.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/mdlmdxreader.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/tgareader.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/tgawriter.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/tpcreader.cpp
    ${TESTS_SOURCE_DIR}/graphics/shadersemantics.cpp
    ${TESTS_SOURCE_DIR}/graphics/format/txireader.cpp
    ${TESTS_SOURCE_DIR}/graphics/keyframetrack.cpp
    ${TESTS_SOURCE_DIR}/graphics/preset.cpp
    ${TESTS_SOURCE_DIR}/graphics/surfacedetail.cpp
    ${TESTS_SOURCE_DIR}/graphics/grading.cpp
    ${TESTS_SOURCE_DIR}/graphics/walkmesh.cpp
    ${TESTS_SOURCE_DIR}/gui/control/progressbar.cpp
    ${TESTS_SOURCE_DIR}/gui/controlrender.cpp
    ${TESTS_SOURCE_DIR}/json/gff.cpp
    ${TESTS_SOURCE_DIR}/resource/archivevalidation.cpp
    ${TESTS_SOURCE_DIR}/resource/extractresources.cpp
    ${TESTS_SOURCE_DIR}/resource/modulediscovery.cpp
    ${TESTS_SOURCE_DIR}/resource/moduleloading.cpp
    ${TESTS_SOURCE_DIR}/resource/modulemount.cpp
    ${TESTS_SOURCE_DIR}/resource/modulepolicy.cpp
    ${TESTS_SOURCE_DIR}/resource/k2merge.cpp
    ${TESTS_SOURCE_DIR}/resource/presentationresources.cpp
    ${TESTS_SOURCE_DIR}/resource/replacements.cpp
    ${TESTS_SOURCE_DIR}/resource/format/2dareader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/2dawriter.cpp
    ${TESTS_SOURCE_DIR}/resource/format/bifreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/erfreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/erfwriter.cpp
    ${TESTS_SOURCE_DIR}/resource/format/gffreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/gffwriter.cpp
    ${TESTS_SOURCE_DIR}/resource/format/keyreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/lytreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/malformed.cpp
    ${TESTS_SOURCE_DIR}/resource/format/rimreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/rimwriter.cpp
    ${TESTS_SOURCE_DIR}/resource/format/ssfreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/tlkreader.cpp
    ${TESTS_SOURCE_DIR}/resource/format/tlkwriter.cpp
    ${TESTS_SOURCE_DIR}/resource/parser/gvt.cpp
    ${TESTS_SOURCE_DIR}/resource/parser/jrl.cpp
    ${TESTS_SOURCE_DIR}/resource/k1startup.cpp
    ${TESTS_SOURCE_DIR}/resource/lookup.cpp
    ${TESTS_SOURCE_DIR}/resource/provider/2das.cpp
    ${TESTS_SOURCE_DIR}/resource/provider/audioclips.cpp
    ${TESTS_SOURCE_DIR}/resource/provider/dialogs.cpp
    ${TESTS_SOURCE_DIR}/resource/provider/gffs.cpp
    ${TESTS_SOURCE_DIR}/resource/provider/scripts.cpp
    ${TESTS_SOURCE_DIR}/resource/2da.cpp
    ${TESTS_SOURCE_DIR}/resource/resources.cpp
    ${TESTS_SOURCE_DIR}/resource/resref.cpp
    ${TESTS_SOURCE_DIR}/resource/saveworkingstate.cpp
    ${TESTS_SOURCE_DIR}/resource/sourcebuckets.cpp
    ${TESTS_SOURCE_DIR}/resource/sourcelifetimes.cpp
    ${TESTS_SOURCE_DIR}/resource/strings.cpp
    ${TESTS_SOURCE_DIR}/scene/childorder.cpp
    ${TESTS_SOURCE_DIR}/scene/emitter.cpp
    ${TESTS_SOURCE_DIR}/scene/emitterbehaviour.cpp
    ${TESTS_SOURCE_DIR}/scene/interp.cpp
    ${TESTS_SOURCE_DIR}/scene/mesh.cpp
    ${TESTS_SOURCE_DIR}/scene/meshdetails.cpp
    ${TESTS_SOURCE_DIR}/scene/model.cpp
    ${TESTS_SOURCE_DIR}/scene/objectlights.cpp
    ${TESTS_SOURCE_DIR}/scene/p1render.cpp
    ${TESTS_SOURCE_DIR}/scene/particleutil.cpp
    ${TESTS_SOURCE_DIR}/script/format/ncsreader.cpp
    ${TESTS_SOURCE_DIR}/script/format/ncswriter.cpp
    ${TESTS_SOURCE_DIR}/script/virtualmachine.cpp
    ${TESTS_SOURCE_DIR}/graphics/fonttext.cpp
    ${TESTS_SOURCE_DIR}/system/arrayref.cpp
    ${TESTS_SOURCE_DIR}/system/binaryreader.cpp
    ${TESTS_SOURCE_DIR}/system/binarywriter.cpp
    ${TESTS_SOURCE_DIR}/system/cache.cpp
    ${TESTS_SOURCE_DIR}/system/cast.cpp
    ${TESTS_SOURCE_DIR}/system/fileutil.cpp
    ${TESTS_SOURCE_DIR}/system/fixedstep.cpp
    ${TESTS_SOURCE_DIR}/system/hexutil.cpp
    ${TESTS_SOURCE_DIR}/system/portablemath.cpp
    ${TESTS_SOURCE_DIR}/system/randomutil.cpp
    ${TESTS_SOURCE_DIR}/system/smallset.cpp
    ${TESTS_SOURCE_DIR}/system/smallvector.cpp
    ${TESTS_SOURCE_DIR}/system/stream/fileinput.cpp
    ${TESTS_SOURCE_DIR}/system/stream/fileoutput.cpp
    ${TESTS_SOURCE_DIR}/system/stream/memoryinput.cpp
    ${TESTS_SOURCE_DIR}/system/stream/memoryoutput.cpp
    ${TESTS_SOURCE_DIR}/system/stringbuilder.cpp
    ${TESTS_SOURCE_DIR}/system/stringutil.cpp
    ${TESTS_SOURCE_DIR}/system/textbuffer.cpp
    ${TESTS_SOURCE_DIR}/system/textreader.cpp
    ${TESTS_SOURCE_DIR}/system/textwriter.cpp
    ${TESTS_SOURCE_DIR}/system/threadpool.cpp
    ${TESTS_SOURCE_DIR}/system/timer.cpp
    ${TESTS_SOURCE_DIR}/system/textencoding.cpp
    ${TESTS_SOURCE_DIR}/system/unicodeutil.cpp
    ${TESTS_SOURCE_DIR}/tools/lip/audioanalyzer.cpp
    ${TESTS_SOURCE_DIR}/tools/lip/composer.cpp
    ${TESTS_SOURCE_DIR}/tools/script/exprtree.cpp
    ${TESTS_SOURCE_DIR}/tools/script/exprtreeoptimizer.cpp)

# Engine app code has no library of its own; compile the socket-free pieces
# under test directly.
list(APPEND TESTS_SOURCES ${CMAKE_SOURCE_DIR}/src/apps/engine/controlprotocol.cpp)
list(APPEND TESTS_SOURCES ${CMAKE_SOURCE_DIR}/src/apps/engine/optionsparser.cpp)

add_executable(tests ${TESTS_HEADERS} ${TESTS_SOURCES} ${CLANG_FORMAT_PATH})
set_target_properties(tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<$<CONFIG:Debug>:/debug>/bin)
target_include_directories(tests PRIVATE ${GTEST_INCLUDE_DIRS})

target_precompile_headers(tests PRIVATE ${CMAKE_SOURCE_DIR}/src/pch.h)
target_link_libraries(tests PRIVATE tools texgen json ${Boost_JSON_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} GTest::gmock_main)

if(MSVC)
    target_compile_options(tests PRIVATE /bigobj)
endif()

add_test(NAME UnitTests COMMAND tests)
# Every test must pass whatever ran before it: random order, several seeds.
add_test(NAME UnitTestsShuffled COMMAND tests --gtest_shuffle --gtest_repeat=3)
