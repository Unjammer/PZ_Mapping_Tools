#!/bin/bash

pz_usage()
{
    cat <<'USAGE'
PZ Mapping Tools - configuration et compilation pour macOS

Utilisation : /bin/bash Setup-macOS.command [options]

  --directory CHEMIN    Dossier de travail, par defaut ~/Developer/PZTools
  --source-dir CHEMIN   Utiliser une copie locale complete des sources
  --ref NOM             Branche ou tag a telecharger, par defaut main
  --update              Mettre a jour un clone propre de la branche choisie
  --dependencies-only   Installer les outils sans telecharger ni compiler le projet
  --no-build            Installer les outils et recuperer les sources
  --jobs N              Nombre de compilations simultanees, par defaut 8 maximum
  --help                Afficher cette aide

Sans --source-dir, une copie complete situee a cote du script est utilisee.
Sinon, les sources sont clonees depuis github.com/Unjammer/PZ_Mapping_Tools.
Les copies locales ne sont jamais modifiees par --update.
Relancer la meme commande reprend les etapes deja preparees.
USAGE
}

pz_fail()
{
    printf '\nErreur : %s\n' "$*" >&2
    if [[ -n "${PZ_LOG:-}" ]]; then
        printf 'Journal : %s\n' "$PZ_LOG" >&2
    fi
    exit 1
}

pz_on_error()
{
    local status="$1"
    local line="$2"
    trap - ERR
    printf '\nEchec pendant : %s (code %s, ligne %s).\n' "$PZ_STEP" "$status" "$line" >&2
    printf 'Journal complet : %s\n' "$PZ_LOG" >&2
    printf 'Les sources et les fichiers de compilation sont conserves.\n' >&2
    exit "$status"
}

pz_value_required()
{
    [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || pz_fail "Valeur manquante pour $1."
}

pz_parse()
{
    PZ_ROOT="$HOME/Developer/PZTools"
    PZ_SOURCE=''
    PZ_REF=main
    PZ_REF_EXPLICIT=0
    PZ_UPDATE=0
    PZ_DEPENDENCIES_ONLY=0
    PZ_NO_BUILD=0
    PZ_JOBS=''
    PZ_HELP=0
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --directory) pz_value_required "$@"; PZ_ROOT="$2"; shift 2 ;;
            --source-dir) pz_value_required "$@"; PZ_SOURCE="$2"; shift 2 ;;
            --ref) pz_value_required "$@"; PZ_REF="$2"; PZ_REF_EXPLICIT=1; shift 2 ;;
            --jobs) pz_value_required "$@"; PZ_JOBS="$2"; shift 2 ;;
            --update) PZ_UPDATE=1; shift ;;
            --dependencies-only) PZ_DEPENDENCIES_ONLY=1; shift ;;
            --no-build) PZ_NO_BUILD=1; shift ;;
            --help|-h) PZ_HELP=1; shift ;;
            *) pz_fail "Option inconnue : $1. Utiliser --help." ;;
        esac
    done
    if [[ -n "$PZ_JOBS" ]]; then
        [[ "$PZ_JOBS" =~ ^[1-9][0-9]*$ && ${#PZ_JOBS} -le 3 ]] || pz_fail '--jobs attend un entier entre 1 et 999.'
    fi
    if [[ -n "$PZ_SOURCE" && ( "$PZ_UPDATE" == 1 || "$PZ_REF_EXPLICIT" == 1 ) ]]; then
        pz_fail '--source-dir utilise les fichiers locaux tels quels. Retirer --update et --ref.'
    fi
}

pz_apple_tools()
{
    PZ_STEP='outils Apple'
    printf '\n[1/5] Verification de Xcode et du SDK macOS\n'
    local developer_path
    developer_path="$(xcode-select -p 2>/dev/null || true)"
    if [[ ! -x "$developer_path/usr/bin/xcodebuild" ]]; then
        if [[ -x /Applications/Xcode.app/Contents/Developer/usr/bin/xcodebuild ]]; then
            sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
        else
            printf 'Installer Xcode avec le SDK macOS, ouvrir Xcode une fois, puis relancer ce script.\n'
            printf 'Page Apple : https://apps.apple.com/app/xcode/id497799835\n'
            open 'https://apps.apple.com/app/xcode/id497799835' || true
            pz_fail 'Xcode complet est necessaire pour cette configuration. Les Command Line Tools seuls ne suffisent pas.'
        fi
    fi
    if ! xcodebuild -checkFirstLaunchStatus; then
        printf 'Initialisation des composants Xcode. macOS peut demander le mot de passe administrateur.\n'
        if ! sudo xcodebuild -runFirstLaunch; then
            pz_fail 'Ouvrir Xcode pour terminer son installation et accepter sa licence, puis relancer le script.'
        fi
    fi
    xcodebuild -version
    PZ_SDK="$(xcrun --sdk macosx --show-sdk-path)"
    [[ -d "$PZ_SDK" ]] || pz_fail 'SDK macOS introuvable. Installer la plateforme macOS dans Xcode.'
    xcrun --sdk macosx --find clang++
    git --version
    make --version
}

pz_homebrew()
{
    PZ_STEP='installation de Homebrew'
    printf '\n[2/5] Verification de Homebrew\n'
    if [[ "$PZ_ARCH" == arm64 ]]; then
        PZ_BREW=/opt/homebrew/bin/brew
    else
        PZ_BREW=/usr/local/bin/brew
    fi
    if [[ ! -x "$PZ_BREW" ]]; then
        printf 'Installation depuis le script officiel Homebrew. Suivre ses confirmations.\n'
        local installer="$PZ_LOG_DIR/homebrew-install.sh"
        curl --fail --show-error --location --proto '=https' --tlsv1.2 \
            --connect-timeout 30 --retry 3 \
            'https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh' \
            --output "$installer"
        /bin/bash "$installer"
    fi
    [[ -x "$PZ_BREW" ]] || pz_fail "Homebrew introuvable a l'emplacement natif attendu : $PZ_BREW"
    eval "$("$PZ_BREW" shellenv)"
    "$PZ_BREW" --version
}

pz_qt()
{
    PZ_STEP='installation de Qt 5'
    printf '\n[3/5] Verification de Qt 5\n'
    if ! "$PZ_BREW" list --versions qt@5 >/dev/null 2>&1; then
        "$PZ_BREW" install qt@5
    fi
    PZ_QT="$("$PZ_BREW" --prefix qt@5)"
    PZ_QMAKE="$PZ_QT/bin/qmake"
    [[ -x "$PZ_QMAKE" ]] || pz_fail "qmake manque dans $PZ_QT."
    PZ_QT_VERSION="$("$PZ_QMAKE" -query QT_VERSION)"
    case "$PZ_QT_VERSION" in
        5.15.*) ;;
        *) pz_fail "Qt 5.15 est attendu, version trouvee : $PZ_QT_VERSION" ;;
    esac
    local tool
    for tool in moc uic rcc lrelease macdeployqt; do
        [[ -x "$PZ_QT/bin/$tool" ]] || pz_fail "Outil Qt manquant : $tool"
    done
    local core="$PZ_QT/lib/QtCore.framework/Versions/5/QtCore"
    if [[ ! -f "$core" ]]; then
        core="$PZ_QT/lib/libQt5Core.5.dylib"
    fi
    [[ -f "$core" ]] || pz_fail "Bibliotheque QtCore introuvable dans $PZ_QT."
    lipo -verify_arch "$PZ_ARCH" "$PZ_QMAKE"
    lipo -verify_arch "$PZ_ARCH" "$core"
    export PATH="$PZ_QT/bin:$PATH"
    "$PZ_QMAKE" -v
}

pz_validate_source()
{
    local file
    for file in TileZed/tiled.pro WorldEd/PZWorldEd.pro shared/pztoolsbuild.h \
        TileZed/.qmake.cache.in WorldEd/.qmake.cache.in; do
        [[ -f "$PZ_SOURCE/$file" ]] || pz_fail "Source incomplete, fichier manquant : $PZ_SOURCE/$file"
    done
    for file in shared/macos.pri shared/macosbundle.pri shared/pztoolspaths.h \
        TileZed/src/tiled/BuildingEd-Info.plist; do
        if [[ ! -f "$PZ_SOURCE/$file" ]]; then
            printf 'Cette revision ne contient pas les corrections de compilation macOS.\n' >&2
            printf 'Utiliser une copie complete recente avec --source-dir, ou relancer avec --update apres leur publication.\n' >&2
            pz_fail "Fichier requis absent : $file"
        fi
    done
}

pz_sources()
{
    PZ_STEP='recuperation des sources'
    printf '\n[4/5] Preparation des sources\n'
    local adjacent
    adjacent="$(cd "$PZ_SCRIPT_DIR/.." && pwd -P)"
    if [[ -z "$PZ_SOURCE" && "$PZ_UPDATE" == 0 && "$PZ_REF_EXPLICIT" == 0 \
        && -f "$adjacent/TileZed/tiled.pro" && -f "$adjacent/WorldEd/PZWorldEd.pro" ]]; then
        PZ_SOURCE="$adjacent"
    fi
    if [[ -n "$PZ_SOURCE" ]]; then
        [[ -d "$PZ_SOURCE" ]] || pz_fail "Dossier source introuvable : $PZ_SOURCE"
        PZ_SOURCE="$(cd "$PZ_SOURCE" && pwd -P)"
        printf 'Copie locale conservee : %s\n' "$PZ_SOURCE"
    else
        PZ_SOURCE="$PZ_ROOT/source"
        [[ ! -L "$PZ_SOURCE" ]] || pz_fail 'Le dossier source est un lien. Utiliser --source-dir pour cette copie locale.'
        if [[ ! -e "$PZ_SOURCE" ]]; then
            git clone --single-branch --branch "$PZ_REF" -- \
                https://github.com/Unjammer/PZ_Mapping_Tools.git "$PZ_SOURCE"
        elif [[ "$PZ_UPDATE" == 1 ]]; then
            [[ -d "$PZ_SOURCE/.git" ]] || pz_fail '--update exige un clone Git dans le dossier source.'
            local origin branch
            origin="$(git -C "$PZ_SOURCE" remote get-url origin)"
            case "$origin" in
                https://github.com/Unjammer/PZ_Mapping_Tools.git|https://github.com/Unjammer/PZ_Mapping_Tools) ;;
                *) pz_fail "Le depot existant utilise une autre origine : $origin" ;;
            esac
            [[ -z "$(git -C "$PZ_SOURCE" status --porcelain)" ]] || pz_fail 'Modifications locales presentes. Les conserver avant de demander --update.'
            branch="$(git -C "$PZ_SOURCE" symbolic-ref --quiet --short HEAD || true)"
            [[ "$branch" == "$PZ_REF" ]] || pz_fail "La copie existante ne suit pas la branche $PZ_REF. Utiliser un autre --directory pour changer de revision."
            git -C "$PZ_SOURCE" fetch origin "$PZ_REF"
            git -C "$PZ_SOURCE" merge --ff-only FETCH_HEAD
        else
            printf 'Sources existantes conservees. --update demande une mise a jour explicite.\n'
            if [[ "$PZ_REF_EXPLICIT" == 1 ]]; then
                pz_fail '--ref selectionne une nouvelle copie. Utiliser un autre --directory ou --update sur la meme branche.'
            fi
        fi
    fi
    pz_validate_source
    printf 'Sources : %s\n' "$PZ_SOURCE"
    if [[ -d "$PZ_SOURCE/.git" ]]; then
        git -C "$PZ_SOURCE" log -1 --format='Revision : %H'
    fi
}

pz_build_project()
{
    local directory="$1"
    local project="$2"
    mkdir -p "$directory"
    (
        cd "$directory"
        "$PZ_QMAKE" "$project" -spec macx-clang CONFIG+=release \
            "QMAKE_APPLE_DEVICE_ARCHS=$PZ_ARCH"
        make -j"$PZ_JOBS"
    )
}

pz_compile()
{
    PZ_STEP='verification du compilateur et de Qt'
    printf '\n[5/5] Verification du compilateur, puis compilation des trois outils\n'
    local fingerprint
    fingerprint="$(printf '%s\n' "$PZ_SOURCE" "$PZ_ARCH" "$PZ_QT" "$PZ_QT_VERSION" "$PZ_SDK" \
        "$(xcrun clang++ --version)" | shasum -a 256)"
    fingerprint="${fingerprint%% *}"
    PZ_BUILD="$PZ_ROOT/build/$PZ_ARCH-${fingerprint:0:12}"
    local probe="$PZ_BUILD/qt-check"
    mkdir -p "$probe"
    cat > "$probe/check.pro" <<'PROJECT'
TEMPLATE = app
TARGET = qt-check
CONFIG += console c++17
CONFIG -= app_bundle
QT += core gui widgets xml svg opengl network concurrent
SOURCES += check.cpp
PROJECT
    cat > "$probe/check.cpp" <<'SOURCE'
#include <QCoreApplication>
#include <QSysInfo>
#include <QDomDocument>
#include <QSvgRenderer>
#include <QGLFormat>
#include <QNetworkAccessManager>
#include <QtConcurrent>
#include <cstdio>
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QByteArray architecture = QSysInfo::currentCpuArchitecture().toUtf8();
    std::printf("Qt %s, architecture %s\n", qVersion(), architecture.constData());
    return 0;
}
SOURCE
    pz_build_project "$probe" "$probe/check.pro"
    "$probe/qt-check"
    PZ_STEP='compilation de TileZed et BuildingEd'
    pz_build_project "$PZ_BUILD/tilezed" "$PZ_SOURCE/TileZed/tiled.pro"
    PZ_STEP='compilation de WorldEd'
    pz_build_project "$PZ_BUILD/worlded" "$PZ_SOURCE/WorldEd/PZWorldEd.pro"
    local application directory executable
    for application in TileZed BuildingEd PZWorldEd; do
        directory=tilezed
        [[ "$application" != PZWorldEd ]] || directory=worlded
        executable="$PZ_BUILD/$directory/bin/$application.app/Contents/MacOS/$application"
        [[ -x "$executable" ]] || pz_fail "Compilation terminee sans l'executable attendu : $executable"
        lipo -verify_arch "$PZ_ARCH" "$executable"
        otool -L "$executable"
    done
    printf '\nCompilation terminee. Applications de developpement :\n'
    printf '  %s\n' "$PZ_BUILD/tilezed/bin/TileZed.app" \
        "$PZ_BUILD/tilezed/bin/BuildingEd.app" "$PZ_BUILD/worlded/bin/PZWorldEd.app"
    printf 'Conserver les dossiers bin et lib ensemble. Les applications utilisent ce Qt local.\n'
    printf 'Au premier lancement, choisir les Tiles. La configuration est dans le bundle, Contents/Config.\n'
    printf 'La compilation ne valide pas le rendu graphique et ne produit pas une distribution signee.\n'
}

pz_main()
{
    set -Eeuo pipefail
    pz_parse "$@"
    if [[ "$PZ_HELP" == 1 ]]; then
        pz_usage
        return 0
    fi
    [[ "$(uname -s)" == Darwin ]] || pz_fail 'Executer ce script sur le Mac, dans Terminal.'
    [[ "$(id -u)" != 0 ]] || pz_fail 'Lancer le script sans sudo. Les installateurs demanderont les droits necessaires.'
    PZ_SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
    if [[ "$(sysctl -in sysctl.proc_translated 2>/dev/null || true)" == 1 ]]; then
        printf 'Passage au Terminal natif Apple Silicon pour la compilation.\n'
        exec /usr/bin/arch -arm64 /bin/bash "$PZ_SCRIPT_DIR/$(basename "${BASH_SOURCE[0]}")" "$@"
    fi
    PZ_ARCH="$(uname -m)"
    case "$PZ_ARCH" in arm64|x86_64) ;; *) pz_fail "Architecture non geree : $PZ_ARCH" ;; esac
    [[ "$PZ_ROOT" != / && "$PZ_ROOT" != "$HOME" ]] || pz_fail 'Choisir un sous-dossier de travail avec --directory.'
    mkdir -p "$PZ_ROOT"
    PZ_ROOT="$(cd "$PZ_ROOT" && pwd -P)"
    [[ "$PZ_ROOT" != / && "$PZ_ROOT" != "$(cd "$HOME" && pwd -P)" ]] || pz_fail 'Choisir un sous-dossier de travail avec --directory.'
    PZ_LOG_DIR="$PZ_ROOT/logs/$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p "$PZ_LOG_DIR"
    PZ_LOG="$PZ_LOG_DIR/setup.log"
    PZ_STEP='initialisation'
    exec > >(tee -a "$PZ_LOG") 2>&1
    trap 'pz_on_error "$?" "$LINENO"' ERR
    if [[ -z "$PZ_JOBS" ]]; then
        PZ_JOBS="$(sysctl -n hw.ncpu)"
        [[ "$PZ_JOBS" -le 8 ]] || PZ_JOBS=8
    fi
    printf 'PZ Mapping Tools - macOS %s - %s\n' "$(sw_vers -productVersion)" "$PZ_ARCH"
    printf 'Dossier de travail : %s\nJournal : %s\n' "$PZ_ROOT" "$PZ_LOG"
    pz_apple_tools
    pz_homebrew
    pz_qt
    if [[ "$PZ_DEPENDENCIES_ONLY" == 1 ]]; then
        printf '\nEnvironnement installe. Relancer sans --dependencies-only pour les sources et la compilation.\n'
        return 0
    fi
    pz_sources
    if [[ "$PZ_NO_BUILD" == 1 ]]; then
        printf '\nEnvironnement et sources prets. Relancer sans --no-build pour compiler.\n'
        return 0
    fi
    pz_compile
    printf 'Journal complet : %s\n' "$PZ_LOG"
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    pz_main "$@"
fi
