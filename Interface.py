import json
import hashlib
import math
import os
import subprocess
import sys
import tempfile
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parent
MAX_REQUEST_BYTES = 1_000_000
CRITERIA = ("sunlight", "earth", "ice", "temp", "smooth")
ACTIVE_CRITERIA = ("sunlight", "earth", "ice", "temp", "smooth")
SITE_NAMES = {
    "Site01": "Connecting ridge",
    "Site04": "Shackleton rim",
    "Site07": "Peak near Shackleton",
    "Site11": "de Gerlache rim",
    "Site23": "Malapert massif",
}
ANALYSIS_LOCK = threading.Lock()
BUILD_STATE: dict[str, str | None] = {"error": None}
WEB_FILES = (
    "index.html",
    "analysis.js",
    "Controls.js",
    "noise field.js",
    "map.js",
    "result.js",
)
REBUILD_FILES = (
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
    "lunar_ranker.cpp",
    "points_heatmap.cpp",
    "raster_preview.cpp",
)


def executable(name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    configuration = "Release" if os.name == "nt" else ""
    return ROOT / "build" / configuration / f"{name}{suffix}"


def runtime_environment() -> dict[str, str]:
    environment = os.environ.copy()
    vcpkg_root = environment.get("VCPKG_ROOT")
    triplet = "x64-windows" if os.name == "nt" else "x64-linux"
    installed = ROOT / "build" / "vcpkg_installed" / triplet
    if not installed.is_dir() and vcpkg_root:
        installed = Path(vcpkg_root) / "installed" / triplet
    if installed.is_dir():
        runtime_directory = installed / "bin"
        if runtime_directory.is_dir():
            environment["PATH"] = str(runtime_directory) + os.pathsep + environment.get("PATH", "")
        data_directory = installed / "share" / "gdal"
        projection_directory = installed / "share" / "proj"
        if data_directory.is_dir():
            environment["GDAL_DATA"] = str(data_directory)
        if projection_directory.is_dir():
            environment["PROJ_DATA"] = str(projection_directory)
            environment["PROJ_LIB"] = str(projection_directory)
    return environment


def cmake_environment() -> dict[str, str]:
    environment = os.environ.copy()
    if environment.get("VCPKG_ROOT"):
        return environment
    cache = ROOT / "build" / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_TOOLCHAIN_FILE:") and "=" in line:
                toolchain = Path(line.split("=", 1)[1])
                if toolchain.name.casefold() == "vcpkg.cmake":
                    root = toolchain.parents[2]
                    if (root / "scripts" / "buildsystems" / "vcpkg.cmake").is_file():
                        environment["VCPKG_ROOT"] = str(root)
                        break
    return environment


def find_tiff(filename: str) -> Path:
    for path in (ROOT / "Tiff Files").rglob("*"):
        if path.is_file() and path.name.casefold() == filename.casefold():
            return path
    raise RuntimeError(f"Required TIFF was not found: {filename}")


def validate_request(payload: Any) -> dict[str, Any]:
    if not isinstance(payload, dict) or not isinstance(payload.get("weights"), dict):
        raise ValueError("Request must contain a weights object.")
    weights = payload["weights"]
    checked_weights: dict[str, float] = {}
    for criterion in CRITERIA:
        value = weights.get(criterion)
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"Weight '{criterion}' must be a number.")
        try:
            numeric_value = float(value)
        except OverflowError as error:
            raise ValueError(f"Weight '{criterion}' must be between 0 and 100.") from error
        if not math.isfinite(numeric_value) or not 0 <= numeric_value <= 100:
            raise ValueError(f"Weight '{criterion}' must be between 0 and 100.")
        checked_weights[criterion] = numeric_value
    if sum(checked_weights[key] for key in ACTIVE_CRITERIA) <= 0:
        raise ValueError("At least one available criterion must have a weight above zero.")

    limits = payload.get("limits")
    if not isinstance(limits, dict):
        raise ValueError("Request must contain a limits object.")
    max_slope = limits.get("maxSlope")
    if isinstance(max_slope, bool) or not isinstance(max_slope, (int, float)):
        raise ValueError("Maximum slope must be a number.")
    try:
        numeric_max_slope = float(max_slope)
    except OverflowError as error:
        raise ValueError("Maximum slope must be between 2 and 20 degrees.") from error
    if not math.isfinite(numeric_max_slope) or not 2 <= numeric_max_slope <= 20:
        raise ValueError("Maximum slope must be between 2 and 20 degrees.")

    return {
        "weights": checked_weights,
        "limits": {"maxSlope": numeric_max_slope},
    }


def run_analysis(payload: dict[str, Any]) -> tuple[bytes, dict[str, str]]:
    if BUILD_STATE["error"]:
        raise RuntimeError(f"Automatic C++ build failed: {BUILD_STATE['error']}")
    ranker = executable("lunar_ranker")
    colorizer = executable("points_heatmap")
    for program in (ranker, colorizer):
        if not program.is_file():
            raise RuntimeError(f"Required executable is missing: {program.name}. Build the project first.")

    with tempfile.TemporaryDirectory(
        prefix="sunlit-summit-", dir=ROOT / "build"
    ) as directory:
        output = Path(directory)
        scores_path = output / "scores.tif"
        color_tiff = output / "suitability.tif"
        color_png = output / "suitability.png"
        published_tiff = ROOT / "build" / "suitability_output.tif"
        published_tiff.parent.mkdir(parents=True, exist_ok=True)
        request = json.dumps(payload)
        environment = runtime_environment()

        ranked = subprocess.run(
            [str(ranker), str(ROOT / "Tiff Files"), str(scores_path)],
            input=request,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=300,
            check=False,
            env=environment,
        )
        if ranked.returncode != 0:
            detail = ranked.stderr.strip() or ranked.stdout.strip() or "lunar_ranker failed."
            raise RuntimeError(detail)
        try:
            metadata = json.loads(ranked.stdout)
        except json.JSONDecodeError as error:
            raise RuntimeError("lunar_ranker returned invalid metadata.") from error

        colored = subprocess.run(
            [str(colorizer), str(scores_path), str(color_tiff), str(color_png)],
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=300,
            check=False,
            env=environment,
        )
        if colored.returncode != 0:
            detail = colored.stderr.strip() or colored.stdout.strip() or "points_heatmap failed."
            raise RuntimeError(detail)

        image = color_png.read_bytes()
        os.replace(color_tiff, published_tiff)
        best = metadata["best"]
        polar_scored = metadata["polarScoredPixels"]
        total_pixels = metadata["totalPixels"]
        fallback_pixels = metadata["fallbackPixels"]
        if metadata["gradedPixels"] != total_pixels:
            raise RuntimeError("lunar_ranker did not grade every global pixel.")
        polar_coverage = 100.0 * polar_scored / total_pixels
        fallback_method = (
            "terrain slope only"
            if payload["weights"]["smooth"] > 0
            else "a neutral score because smooth-terrain weight is zero"
        )
        headers = {
            "X-Raster-Width": str(metadata["width"]),
            "X-Raster-Height": str(metadata["height"]),
            "X-Best-X": str(best["x"]),
            "X-Best-Y": str(best["y"]),
            "X-Graded-Pixels": str(metadata["gradedPixels"]),
            "X-Total-Pixels": str(total_pixels),
            "X-Polar-Scored-Pixels": str(polar_scored),
            "X-Fallback-Pixels": str(fallback_pixels),
            "X-Scoring-Note": (
                f"Every global pixel is graded. {polar_coverage:.1f}% use the south-polar science layers; "
                f"the rest use {fallback_method} and do not use south-polar science data. WAC and section-2 "
                "sunlight are averaged where they overlap. Temperature is a sunlight/shadow "
                "balance proxy, not measured stability or ice abundance."
            ),
            "X-GeoTIFF-URL": f"/build/suitability_output.tif?v={time.time_ns()}",
        }
        return image, headers


def run_site_analysis(
    payload: dict[str, Any], site_id: str
) -> tuple[bytes, dict[str, str]]:
    if site_id not in SITE_NAMES:
        raise ValueError(f"Unknown finalist site: {site_id}")
    if BUILD_STATE["error"]:
        raise RuntimeError(f"Automatic C++ build failed: {BUILD_STATE['error']}")
    ranker = executable("lunar_ranker")
    colorizer = executable("points_heatmap")
    for program in (ranker, colorizer):
        if not program.is_file():
            raise RuntimeError(
                f"Required executable is missing: {program.name}. Build the project first."
            )

    with tempfile.TemporaryDirectory(
        prefix=f"sunlit-{site_id.casefold()}-", dir=ROOT / "build"
    ) as directory:
        output = Path(directory)
        scores_path = output / "scores.tif"
        color_tiff = output / "suitability.tif"
        color_png = output / "suitability.png"
        published_tiff = ROOT / "build" / f"suitability_{site_id}.tif"
        request = json.dumps(payload)
        environment = runtime_environment()
        ranked = subprocess.run(
            [
                str(ranker),
                str(ROOT / "Tiff Files"),
                str(scores_path),
                "--site",
                site_id,
            ],
            input=request,
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=300,
            check=False,
            env=environment,
        )
        if ranked.returncode != 0:
            detail = (
                ranked.stderr.strip()
                or ranked.stdout.strip()
                or "lunar_ranker finalist analysis failed."
            )
            raise RuntimeError(detail)
        try:
            metadata = json.loads(ranked.stdout)
        except json.JSONDecodeError as error:
            raise RuntimeError("lunar_ranker returned invalid finalist metadata.") from error

        colored = subprocess.run(
            [str(colorizer), str(scores_path), str(color_tiff), str(color_png)],
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=300,
            check=False,
            env=environment,
        )
        if colored.returncode != 0:
            detail = (
                colored.stderr.strip()
                or colored.stdout.strip()
                or "points_heatmap finalist colorization failed."
            )
            raise RuntimeError(detail)

        image = color_png.read_bytes()
        os.replace(color_tiff, published_tiff)
        best = metadata["best"]
        valid_pixels = metadata["validPixels"]
        total_pixels = metadata["totalPixels"]
        valid_percent = 100.0 * valid_pixels / total_pixels
        headers = {
            "X-Site-ID": site_id,
            "X-Site-Name": SITE_NAMES[site_id],
            "X-Raster-Width": str(metadata["width"]),
            "X-Raster-Height": str(metadata["height"]),
            "X-Best-X": str(best["x"]),
            "X-Best-Y": str(best["y"]),
            "X-Valid-Pixels": str(valid_pixels),
            "X-Total-Pixels": str(total_pixels),
            "X-Scoring-Note": (
                f"{SITE_NAMES[site_id]} scored at 5 m per pixel using the supplied "
                f"LOLA slope raster and available south-polar layers. {valid_percent:.1f}% "
                "of the window has valid paired elevation and slope; missing cells "
                "are shown with a neutral grade. Surface elevation is context, not "
                "a separate weighted criterion."
            ),
            "X-GeoTIFF-URL": f"/build/suitability_{site_id}.tif?v={time.time_ns()}",
        }
        return image, headers


def run_base_map_preview() -> tuple[bytes, dict[str, str]]:
    previewer = executable("raster_preview")
    if not previewer.is_file():
        raise RuntimeError(
            f"Required executable is missing: {previewer.name}. Build the project first."
        )
    with tempfile.TemporaryDirectory(
        prefix="sunlit-base-", dir=ROOT / "build"
    ) as directory:
        preview_path = Path(directory) / "layer.png"
        previewed = subprocess.run(
            [str(previewer), str(find_tiff("ldem_16_uint.tif")), "elevation",
             str(preview_path)],
            text=True,
            capture_output=True,
            cwd=ROOT,
            timeout=300,
            check=False,
            env=runtime_environment(),
        )
        if previewed.returncode != 0:
            detail = previewed.stderr.strip() or previewed.stdout.strip() or "raster_preview failed."
            raise RuntimeError(detail)
        image = preview_path.read_bytes()
        if image[:8] != b"\x89PNG\r\n\x1a\n":
            raise RuntimeError("raster_preview returned an invalid PNG.")
        width = int.from_bytes(image[16:20], "big")
        height = int.from_bytes(image[20:24], "big")
        return image, {
            "X-Raster-Width": str(width),
            "X-Raster-Height": str(height),
        }


def watched_file_state() -> dict[str, tuple[int, int]]:
    names = ("Interface.py",) + WEB_FILES + REBUILD_FILES
    result: dict[str, tuple[int, int]] = {}
    for name in names:
        path = ROOT / name
        if path.is_file():
            stat = path.stat()
            result[name] = (stat.st_mtime_ns, stat.st_size)
    return result


def development_file_watcher(
    server: ThreadingHTTPServer, restart_requested: threading.Event
) -> None:
    previous = watched_file_state()
    while True:
        time.sleep(0.5)
        current = watched_file_state()
        changed = {name for name in current if current.get(name) != previous.get(name)}
        if not changed:
            continue
        time.sleep(0.75)
        current = watched_file_state()
        if "Interface.py" in changed:
            try:
                compile((ROOT / "Interface.py").read_text(encoding="utf-8"),
                        str(ROOT / "Interface.py"), "exec")
            except (OSError, SyntaxError) as error:
                print(f"Hot reload skipped: Interface.py is invalid: {error}", file=sys.stderr)
                previous = current
                continue
            print("Interface.py changed; restarting the development server.")
            restart_requested.set()
            server.shutdown()
            return
        if changed.intersection(REBUILD_FILES):
            with ANALYSIS_LOCK:
                build = subprocess.run(
                    ["cmake", "--build", "--preset", "dev"],
                    text=True,
                    capture_output=True,
                    cwd=ROOT,
                    check=False,
                    env=cmake_environment(),
                )
            if build.returncode == 0:
                BUILD_STATE["error"] = None
                print("C++ sources changed; development build completed.")
            else:
                BUILD_STATE["error"] = (
                    build.stderr.strip() or build.stdout.strip() or
                    "cmake --build --preset dev failed."
                )
                print(f"Automatic C++ build failed: {BUILD_STATE['error']}", file=sys.stderr)
        previous = watched_file_state()


def web_version() -> str:
    state = watched_file_state()
    fingerprint = hashlib.sha256()
    for name in WEB_FILES:
        fingerprint.update(name.encode("utf-8"))
        fingerprint.update(str(state.get(name, (0, 0))).encode("ascii"))
    return fingerprint.hexdigest()


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args: Any, **kwargs: Any) -> None:
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def send_api_error(self, status: int, message: str) -> None:
        body = json.dumps({"error": message}).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/health":
            body = b'{"status":"ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/version":
            body = json.dumps({"version": web_version()}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if path == "/api/base-map":
            try:
                with ANALYSIS_LOCK:
                    image, headers = run_base_map_preview()
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                self.log_error("Base map preview failed: %s", error)
                self.send_api_error(500, str(error))
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(image)))
            for name, value in headers.items():
                self.send_header(name, value)
            self.end_headers()
            self.wfile.write(image)
            return
        super().do_GET()

    def do_POST(self) -> None:
        request_url = urlparse(self.path)
        site_id = ""
        if request_url.path == "/api/analyze-site":
            query = parse_qs(request_url.query)
            sites = query.get("site", [])
            if len(sites) != 1 or sites[0] not in SITE_NAMES:
                self.send_api_error(400, "A valid finalist site is required.")
                return
            site_id = sites[0]
        elif request_url.path != "/api/analyze":
            self.send_api_error(404, "Unknown API endpoint.")
            return
        if self.headers.get_content_type() != "application/json":
            self.send_api_error(415, "Content-Type must be application/json.")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_api_error(400, "Invalid Content-Length.")
            return
        if length <= 0 or length > MAX_REQUEST_BYTES:
            self.send_api_error(413, "Request body is empty or too large.")
            return

        try:
            payload = validate_request(json.loads(self.rfile.read(length)))
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError) as error:
            self.send_api_error(400, str(error))
            return

        try:
            with ANALYSIS_LOCK:
                if site_id:
                    image, headers = run_site_analysis(payload, site_id)
                else:
                    image, headers = run_analysis(payload)
        except ValueError as error:
            self.send_api_error(400, str(error))
            return
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            self.log_error("Analysis failed: %s", error)
            self.send_api_error(500, str(error))
            return

        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(image)))
        for name, value in headers.items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(image)


if __name__ == "__main__":
    while True:
        port = int(os.environ.get("PORT", "8765"))
        server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
        restart_requested = threading.Event()
        threading.Thread(
            target=development_file_watcher,
            args=(server, restart_requested),
            daemon=True,
        ).start()
        print(f"Serving the website and analysis API at http://127.0.0.1:{port}")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            break
        finally:
            server.server_close()
        if not restart_requested.is_set():
            break
        original_name = __name__
        globals()["__name__"] = "__hot_reload__"
        try:
            source = (ROOT / "Interface.py").read_text(encoding="utf-8")
            exec(compile(source, str(ROOT / "Interface.py"), "exec"), globals())
        finally:
            globals()["__name__"] = original_name
