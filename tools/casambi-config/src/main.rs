mod cloud;
mod emit;

use anyhow::{Context, Result};
use clap::Parser;
use std::fs::File;
use std::io::{self, Write};
use std::path::PathBuf;

#[derive(Parser, Debug)]
#[command(
    name = "casambi-config",
    about = "Fetch Casambi Evolution network keys and emit ESPHome YAML"
)]
struct Args {
    /// Gateway BLE MAC address (e.g. AA:BB:CC:DD:EE:FF)
    #[arg(long, short = 'a')]
    address: String,

    /// Casambi network password
    #[arg(long, short = 'p')]
    password: String,

    /// Output path (default: stdout). Use '-' for stdout.
    #[arg(long, short = 'o', default_value = "-")]
    output: PathBuf,
}

fn main() -> Result<()> {
    let args = Args::parse();
    eprintln!(
        "Fetching Casambi Evolution config for {} ...",
        args.address
    );

    let info = cloud::fetch_network_config(&args.address, &args.password)
        .context("cloud fetch failed")?;

    eprintln!(
        "Network '{}' (protocol v{}), {} unit(s), using key id={} role={}",
        info.network_name,
        info.protocol_version,
        info.units.len(),
        info.key.id,
        info.key.role
    );

    if args.output.as_os_str() == "-" {
        emit::emit_yaml(&info, &mut io::stdout())?;
    } else {
        let mut file = File::create(&args.output)
            .with_context(|| format!("create {}", args.output.display()))?;
        emit::emit_yaml(&info, &mut file)?;
        file.flush()?;
        eprintln!("Wrote {}", args.output.display());
    }

    Ok(())
}
