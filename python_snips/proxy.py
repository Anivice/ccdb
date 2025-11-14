# Fetch all proxies and groups
resp = requests.get(f"{base_url}/proxies", headers=headers)
proxies_info = resp.json()
# proxies_info will be a dict with proxy names and their info (type, history, etc.)
for name, info in proxies_info.get("proxies", {}).items():
    print(f"Proxy: {name}, Type: {info.get('type')}, Now: {info.get('now')}")
