#!/usr/bin/env python

import requests

base_url = "http://127.0.0.1:9090"
secret = ""
headers = {"Authorization": f"Bearer {secret}"}

# Fetch current traffic stats
resp = requests.get(f"{base_url}/traffic", headers=headers)
traffic_data = resp.json()
print("Current Upload:", traffic_data.get("up"),
      "Current Download:", traffic_data.get("down"))
print("Total Uploaded:", traffic_data.get("upTotal"),
      "Total Downloaded:", traffic_data.get("downTotal"))
