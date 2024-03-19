
# Defibox-Vault
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/DefiboxTeam/ram.defi/blob/main/LICENSE)
[![Test](https://github.com/DefiboxTeam/ram.defi/actions/workflows/test.yml/badge.svg)](https://github.com/DefiboxTeam/ram.defi/actions/workflows/test.yml)

# Overview
BRAM is a RAM resource certificate issued by Defibox. This certificate can be transferred, traded and participated in more Defi gameplay at will. For example, in the Swap protocol, you can conduct transactions with low fees, in the USN protocol, you can obtain the stable currency USN as collateral, and in the lending protocol, you can lend other tokens as collateral to improve usage efficiency.

# Audits

- <a href="https://github.com/blocksecteam/audit-reports/blob/main/solidity/blocksec_ramdefi_v1.0-signed.pdf"> Blocksec Audit</a>

## Contracts

| name                                                | description     |
| --------------------------------------------------- | --------------- |
| [ram.defi](https://bloks.io/account/ram.defi)       | Ram Contract  |

## Quickstart

### `USER`

```bash
# deposit eos
$ cleos push action eosio.token transfer '["tester1","ram.defi","1.0000 EOS",""]' -p tester1

# deposit ram
$ cleos push action eosio ramtransfer '["tester1","ram.defi",100,""]' -p tester1

# withdraw eos
$ cleos push action ram.defi transfer '["tester1","ram.defi","10000 BRAM",""]' -p tester1

# withdraw ram
$ cleos push action ram.defi transfer '["tester1","ram.defi","10000 BRAM","ram"]' -p tester1

# transfer bram
$ cleos push action ram.defi transfer '["tester1", "tester2", "1 BRAM", ""]' -p tester1
```

### `ADMIN`

```bash
# modify deposit/withdraw status (true: disabled false: enabled)
$ cleos push action ram.defi updatestatus '[true, true]' -p admin.defi

# modify deposit/withdraw fee ratio (ex: 50 -> 0.005) 
$ cleos push action ram.defi updateratio '[50, 50]' -p admin.defi
```

### Viewing Table Information

```bash
cleos get table ram.defi ram.defi config
cleos get table ram.defi tester1 accounts
cleos get table ram.defi BRAM stat
```
