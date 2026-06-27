#!/usr/bin/env python3
"""Verifica parsing de linea STS (misma logica que uart_comm.cpp)."""
from __future__ import annotations


def parse_sts_payload(payload: str) -> dict[str, str]:
    out: dict[str, str] = {}
    start = 0
    while start < len(payload):
        delim = payload.find(";", start)
        if delim == -1:
            delim = len(payload)
        pair = payload[start:delim].strip()
        if "=" in pair:
            eq = pair.index("=")
            key = pair[:eq].strip().upper()
            val = pair[eq + 1 :].strip().replace(",", ".")
            out[key] = val
        start = delim + 1
    return out


def apply_sensor_fields(parsed: dict[str, str]) -> dict:
    st = {
        "ph": 0.0,
        "tds": 0,
        "temp_water": 0.0,
        "temp_water_probe": 0.0,
        "temp_air": 0.0,
        "hum_air": 0.0,
        "dht_online": True,
        "ph_probe_ok": True,
        "tds_probe_ok": True,
        "tw_probe_ok": True,
    }
    dht_seen = False
    dht_ok = False
    for key, val in parsed.items():
        if key == "PH":
            st["ph"] = float(val)
        elif key in ("TDS", "EC", "PPM"):
            st["tds"] = int(float(val))
        elif key == "TW":
            st["temp_water"] = float(val)
        elif key == "TWP":
            st["temp_water_probe"] = float(val)
        elif key in ("TA", "TMPA", "T_AIR"):
            st["temp_air"] = float(val)
        elif key in ("HA", "HUM", "RH"):
            st["hum_air"] = float(val)
        elif key == "DHT":
            dht_seen = True
            dht_ok = val == "1"
        elif key == "PHOK":
            st["ph_probe_ok"] = val == "1"
        elif key == "TDSOK":
            st["tds_probe_ok"] = val == "1"
        elif key == "TWOK":
            st["tw_probe_ok"] = val == "1"
    if dht_seen:
        st["dht_online"] = dht_ok
    return st


def main() -> None:
    sample = (
        "PH=6.12;PHRAW=512;PHOK=1;TDS=450;TDSOK=1;TW=20.1;TWOK=1;TWP=20.0;TA=23.4;HA=55.0;"
        "DHT=1;PHDO=0;NMIN=0;NMAX=0"
    )
    p = parse_sts_payload(sample)
    assert p["PH"] == "6.12"
    assert p["TDS"] == "450"
    assert p["TA"] == "23.4"
    assert p["HA"] == "55.0"
    assert p["DHT"] == "1"

    st = apply_sensor_fields(p)
    assert abs(st["ph"] - 6.12) < 1e-6
    assert st["tds"] == 450
    assert abs(st["temp_air"] - 23.4) < 1e-6
    assert abs(st["hum_air"] - 55.0) < 1e-6
    assert st["dht_online"] is True
    assert st["ph_probe_ok"] is True
    assert st["tds_probe_ok"] is True
    assert st["tw_probe_ok"] is True

    bad_tw = parse_sts_payload("TW=0;TWOK=0;TA=20;HA=50;DHT=1")
    stw = apply_sensor_fields(bad_tw)
    assert stw["tw_probe_ok"] is False

    bad_ph = parse_sts_payload("PH=14;PHOK=0;TDSOK=1;TA=20;HA=50;DHT=1")
    stb = apply_sensor_fields(bad_ph)
    assert stb["ph_probe_ok"] is False
    assert stb["tds_probe_ok"] is True

    # coma decimal
    sample2 = "PH=6,10;TDS=100;TA=22,5;HA=60,0;DHT=0"
    p2 = parse_sts_payload(sample2)
    st2 = apply_sensor_fields(p2)
    assert abs(st2["ph"] - 6.10) < 1e-6
    assert st2["dht_online"] is False

    # alias EC
    pec = parse_sts_payload("EC=333;TA=18;HA=50;DHT=1")
    ste = apply_sensor_fields(pec)
    assert ste["tds"] == 333

    print("verify_sts_parse: OK")


if __name__ == "__main__":
    main()
