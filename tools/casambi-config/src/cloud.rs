use anyhow::{bail, Context, Result};
use serde::Deserialize;
use serde_json::{json, Value};

const API_BASE: &str = "https://api.casambi.com";
const DEVICE_NAME: &str = "casambi-esphome-config";

#[derive(Debug, Clone)]
pub struct NetworkKey {
    pub id: u8,
    pub role: u8,
    pub name: String,
    pub key_hex: String,
}

#[derive(Debug, Clone)]
pub struct UnitInfo {
    pub device_id: u8,
    pub name: String,
}

#[derive(Debug, Clone)]
pub struct NetworkInfo {
    pub network_id: String,
    pub network_name: String,
    pub protocol_version: u8,
    pub mac_address: String,
    pub key: NetworkKey,
    pub units: Vec<UnitInfo>,
}

#[derive(Deserialize)]
struct UuidResponse {
    id: String,
}

#[derive(Deserialize)]
struct SessionResponse {
    session: String,
}

pub fn fetch_network_config(mac_address: &str, password: &str) -> Result<NetworkInfo> {
    let uuid = mac_to_uuid(mac_address);
    let client = reqwest::blocking::Client::builder()
        .user_agent(DEVICE_NAME)
        .timeout(std::time::Duration::from_secs(30))
        .build()?;

    let network_id = get_network_id(&client, &uuid)?;
    let session = create_session(&client, &network_id, password)?;
    let body = fetch_network_update(&client, &network_id, &session)?;
    parse_network(&body, mac_address, &network_id)
}

fn mac_to_uuid(mac: &str) -> String {
    mac.chars()
        .filter(|c| *c != ':')
        .flat_map(|c| c.to_lowercase())
        .collect()
}

fn get_network_id(client: &reqwest::blocking::Client, uuid: &str) -> Result<String> {
    let url = format!("{API_BASE}/network/uuid/{uuid}");
    let resp = client
        .get(&url)
        .send()
        .with_context(|| format!("GET {url}"))?;
    if !resp.status().is_success() {
        bail!("Failed to resolve network UUID (HTTP {}): {}", resp.status(), resp.text().unwrap_or_default());
    }
    let parsed: UuidResponse = resp.json().context("parse uuid response")?;
    Ok(parsed.id)
}

fn create_session(
    client: &reqwest::blocking::Client,
    network_id: &str,
    password: &str,
) -> Result<String> {
    let url = format!("{API_BASE}/network/{network_id}/session");
    let body = json!({
        "token": "",
        "password": password,
        "deviceName": DEVICE_NAME,
    });
    let resp = client
        .post(&url)
        .json(&body)
        .send()
        .with_context(|| format!("POST {url}"))?;
    if !resp.status().is_success() {
        bail!(
            "Failed to create session (HTTP {}). Check network password. Body: {}",
            resp.status(),
            resp.text().unwrap_or_default()
        );
    }
    let parsed: SessionResponse = resp.json().context("parse session response")?;
    Ok(parsed.session)
}

fn fetch_network_update(
    client: &reqwest::blocking::Client,
    network_id: &str,
    session: &str,
) -> Result<Value> {
    let url = format!("{API_BASE}/network/{network_id}/");
    let body = json!({
        "formatVersion": 1,
        "deviceName": DEVICE_NAME,
        "revision": 0,
    });
    let resp = client
        .put(&url)
        .header("X-Casambi-Session", session)
        .json(&body)
        .send()
        .with_context(|| format!("PUT {url}"))?;
    if !resp.status().is_success() {
        bail!(
            "Failed to fetch network config (HTTP {}): {}",
            resp.status(),
            resp.text().unwrap_or_default()
        );
    }
    resp.json().context("parse network update JSON")
}

fn parse_network(root: &Value, mac_address: &str, network_id: &str) -> Result<NetworkInfo> {
    let network = root
        .get("network")
        .context("missing 'network' object — unexpected cloud response")?;

    let network_name = network
        .get("name")
        .and_then(|v| v.as_str())
        .unwrap_or("Casambi")
        .to_string();
    let protocol_version = network
        .get("protocolVersion")
        .and_then(|v| v.as_u64())
        .unwrap_or(0) as u8;

    let key_store = network.get("keyStore").context(
        "No keyStore in network config. This tool only supports Evolution networks (Classic is not supported).",
    )?;
    let keys = key_store
        .get("keys")
        .and_then(|v| v.as_array())
        .context("keyStore.keys missing")?;
    if keys.is_empty() {
        bail!("keyStore.keys is empty");
    }

    let mut best: Option<NetworkKey> = None;
    for key_obj in keys {
        let id = key_obj.get("id").and_then(|v| v.as_u64()).unwrap_or(0) as u8;
        let role = key_obj.get("role").and_then(|v| v.as_u64()).unwrap_or(0) as u8;
        let name = key_obj
            .get("name")
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_string();
        let key_hex = key_obj
            .get("key")
            .and_then(|v| v.as_str())
            .context("key missing hex")?
            .to_lowercase();
        if key_hex.len() != 32 {
            bail!("expected 32-char hex key, got length {}", key_hex.len());
        }
        let candidate = NetworkKey {
            id,
            role,
            name,
            key_hex,
        };
        if best.as_ref().map(|b| candidate.role > b.role).unwrap_or(true) {
            best = Some(candidate);
        }
    }
    let key = best.context("no usable keys")?;

    let mut units = Vec::new();
    if let Some(arr) = network.get("units").and_then(|v| v.as_array()) {
        for unit in arr {
            let device_id = unit
                .get("deviceID")
                .and_then(|v| v.as_u64())
                .unwrap_or(0) as u8;
            let name = unit
                .get("name")
                .and_then(|v| v.as_str())
                .unwrap_or("Unit")
                .to_string();
            units.push(UnitInfo { device_id, name });
        }
        units.sort_by_key(|u| u.device_id);
    }

    Ok(NetworkInfo {
        network_id: network_id.to_string(),
        network_name,
        protocol_version,
        mac_address: normalize_mac(mac_address),
        key,
        units,
    })
}

fn normalize_mac(mac: &str) -> String {
    let hex: String = mac
        .chars()
        .filter(|c| c.is_ascii_hexdigit())
        .flat_map(|c| c.to_uppercase())
        .collect();
    if hex.len() != 12 {
        return mac.to_string();
    }
    format!(
        "{}:{}:{}:{}:{}:{}",
        &hex[0..2],
        &hex[2..4],
        &hex[4..6],
        &hex[6..8],
        &hex[8..10],
        &hex[10..12]
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mac_to_uuid_strips_colons() {
        assert_eq!(mac_to_uuid("AA:BB:CC:DD:EE:FF"), "aabbccddeeff");
    }

    #[test]
    fn normalize_mac_formats() {
        assert_eq!(normalize_mac("aabbccddeeff"), "AA:BB:CC:DD:EE:FF");
    }
}
