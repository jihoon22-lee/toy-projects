"""Optional: uv run --with playwright python scripts/browser_smoke.py REPORT [CHROME]."""

import sys
from pathlib import Path

from playwright.sync_api import sync_playwright

with sync_playwright() as driver:
    kwargs = {"headless": True}
    if len(sys.argv) > 2:
        kwargs["executable_path"] = sys.argv[2]
    browser = driver.chromium.launch(**kwargs)
    page = browser.new_page(viewport={"width": 1280, "height": 800})
    errors = []
    requests = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.on("request", lambda request: requests.append(request.url))
    page.goto(Path(sys.argv[1]).resolve().as_uri())
    page.get_by_role("button", name="Changes", exact=True).click()
    assert "new-failure" in page.locator("#content").inner_text()
    page.locator("#change").select_option("new-failure")
    assert page.locator("tbody tr").count() == 1
    page.locator("#change").select_option("")
    page.get_by_role("button", name="History", exact=True).click()
    assert "1 / 2" in page.locator("#content").inner_text()
    page.get_by_role("button", name="Input quality", exact=True).click()
    assert "VERIFIED" in page.locator("#content").inner_text()
    page.get_by_role("button", name="Results", exact=True).click()
    page.locator("#search").fill("login")
    assert page.locator("tbody tr").count() == 1
    page.locator("summary").first.click()
    assert "Expected 200" in page.locator("pre").first.inner_text()
    for scheme in ["light", "dark"]:
        page.emulate_media(color_scheme=scheme)
        page.screenshot(path=str(Path(sys.argv[1]).with_suffix(f".{scheme}.png")))
    page.set_viewport_size({"width": 390, "height": 844})
    page.screenshot(path=str(Path(sys.argv[1]).with_suffix(".mobile.png")))
    assert not errors, errors
    assert not any(url.startswith(("http:", "https:")) for url in requests), requests
    browser.close()
    print("Offline browser: tabs/search/filters/evidence, light/dark/mobile passed")
