import { Name, Asset, Authority, PermissionLevel, TimePointSec } from "@greymass/eosio";
import { Account, AccountPermission, Blockchain, expectToThrow } from '@proton/vert'

const blockchain = new Blockchain()

const BRAM_CONTRACT = "ram.defi";
const BRAM_SYMBOL = "BRAM";
const EOS_SYMBOL = "EOS";

// contracts
const contracts = {
  ram: blockchain.createContract(BRAM_CONTRACT, 'tests/ram.defi', true),
  eosio: blockchain.createContract('eosio', 'tests/eosio', true),
  eos: blockchain.createContract('eosio.token', 'tests/eosio.token', true),
  box: blockchain.createContract('token.defi', 'tests/eosio.token', true),
}
// permission
const rambank = blockchain.createAccount("rambank.defi");
rambank.setPermissions([AccountPermission.from({
  parent: "owner",
  perm_name: "active",
  required_auth: Authority.from({
    threshold: 1,
    accounts: [{
      weight: 1,
      permission: PermissionLevel.from("ram.defi@eosio.code")
    },
    {
      weight: 1,
      permission: PermissionLevel.from("rambank.defi@eosio.code")
    }
    ]
  })
}),
]);

contracts.eos.setPermissions([AccountPermission.from({
  parent: "owner",
  perm_name: "active",
  required_auth: Authority.from({
    threshold: 1,
    accounts: [{
      weight: 1,
      permission: PermissionLevel.from("eosio@active")
    }]
  })
}),
]);
// accounts
blockchain.createAccounts('admin.defi', "ramfees.defi", 'account1');


interface ConfigRow {
  disabled_deposit: boolean;
  disabled_withdraw: boolean;
  deposit_fee_ratio: number;
  withdraw_fee_ratio: number;
}


interface Connector {
  balance: string;
  weight: number;
}

interface RamMarket {
  supply: Asset;
  base: Connector;
  quote: Connector;
}

function get_bancor_output(inp_reserve: number, out_reserve: number, inp: number): number {
  let out = Math.trunc((inp * out_reserve) / (inp_reserve + inp));
  if (out < 0)
    out = 0;
  return out;
}

function get_fee(quantity: number): number{
  return Math.trunc((quantity + 199) / 200);
}

//sellram
function eos_receive_with_fee(quantity: number): number {
  let ramMarket = getRamMarket();
  let ram_reserve = Asset.from(ramMarket.base.balance).units.toNumber();
  let eos_reserve = Asset.from(ramMarket.quote.balance).units.toNumber();
  let receive = get_bancor_output(ram_reserve, eos_reserve, quantity);
  let fee = get_fee(receive);
  let receive_after_fee = receive - fee;
  return receive_after_fee;
}

//buyram
function bytes_cost_with_fee(quantity: number): number {
  let fee = get_fee(quantity);
  let quantity_after_fee = quantity - fee;
  let ramMarket = getRamMarket();
  let ram_reserve = Asset.from(ramMarket.base.balance).units.toNumber();
  let eos_reserve = Asset.from(ramMarket.quote.balance).units.toNumber();
  let receive = get_bancor_output(eos_reserve, ram_reserve, quantity_after_fee);
  return receive;
}

function deposit_cost_with_fee(quantity: number): number{
  const config = getConfig();
  return quantity - Math.trunc(quantity * config.deposit_fee_ratio / 10000)
}

function withdraw_cost_with_fee(quantity: number): number{
  const config = getConfig();
  return quantity - Math.trunc(quantity * config.withdraw_fee_ratio / 10000)
}

function getConfig(): ConfigRow {
  const scope = Name.from('ram.defi').value.value;
  return contracts.ram.tables.config(scope).getTableRows()[0];
}

function getRamBytes(account: string) {
  const scope = Name.from(account).value.value
  const row = contracts.eosio.tables
    .userres(scope)
    .getTableRow(scope)
  if (!row) return 0
  return row.ram_bytes
}

const getTokenBalance = (account: string, contract: Account, symcode: string): number => {
  let scope = Name.from(account).value.value;
  const primaryKey = Asset.SymbolCode.from(symcode).value.value;
  const result = contract.tables.accounts(scope).getTableRow(primaryKey);
  if (result?.balance) { return Asset.from(result.balance).units.toNumber(); }
  return 0;
}

function getRamMarket(): RamMarket {
  const scope = Name.from("eosio").value.value
  return contracts.eosio.tables
    .rammarket(scope)
    .getTableRows()[0]
}

function getTokenMaxSupply(symcode: string) {
  const scope = Asset.SymbolCode.from(symcode).value.value
  const row = contracts.ram.tables
    .stat(scope)
    .getTableRow(scope)
  if (!row) return 0;
  return Asset.from(row.max_supply).units.toNumber()
}

describe("ram.defi", () => {
  test('token issue', async () => {
    // create BOX token
    await contracts.box.actions.create(["token.defi", "10000000000.000000 BOX"]).send("token.defi@active");
    await contracts.box.actions.issue(["token.defi", "10000000000.000000 BOX", "init"]).send("token.defi@active");
    await contracts.box.actions.transfer(["token.defi", "account1", "100000.000000 BOX", "init"]).send("token.defi@active");
    // create EOS token
    await contracts.eos.actions.create(["eosio.token", "10000000000.0000 EOS"]).send("eosio.token@active");
    await contracts.eos.actions.issue(["eosio.token", "10000000000.0000 EOS", "init"]).send("eosio.token@active");
    await contracts.eos.actions.transfer(["eosio.token", "account1", "100000.0000 EOS", "init"]).send("eosio.token@active");

    await contracts.eosio.actions.init().send();
    // buyram
    await contracts.eosio.actions.buyram(["account1", "account1", "100.0000 EOS"]).send("account1@active");
  })

  test('only admin.defi', async () => {
    let action = contracts.ram.actions.updatestatus([true, true]).send("account1@active");
    await expectToThrow(action, "missing required authority admin.defi");

    action = contracts.ram.actions.updateratio([500, 500]).send("account1@active");
    await expectToThrow(action, "missing required authority admin.defi");
  })

  test('updatestatus', async () => {
    await contracts.ram.actions.updatestatus([true, true]).send("admin.defi@active");

    let config = getConfig();
    expect(config.disabled_withdraw).toEqual(true);
    expect(config.disabled_deposit).toEqual(true);

    await contracts.ram.actions.updatestatus([false, false]).send("admin.defi@active");
    config = getConfig();
    expect(config.disabled_withdraw).toEqual(false);
    expect(config.disabled_deposit).toEqual(false);
  })

  test('updateratio: ratio <= 5000', async () => {
    let action = contracts.ram.actions.updateratio([6000, 500]).send("admin.defi@active");
    await expectToThrow(action, "eosio_assert: ram.defi::updateratio: deposit_fee_ratio must be <= 5000");

    action = contracts.ram.actions.updateratio([500, 6000]).send("admin.defi@active");
    await expectToThrow(action, "eosio_assert: ram.defi::updateratio: withdraw_fee_ratio must be <= 5000");
  })

  test('updateratio', async () => {
    await contracts.ram.actions.updateratio([500, 500]).send("admin.defi@active");

    let config = getConfig();
    expect(config.deposit_fee_ratio).toEqual(500);
    expect(config.withdraw_fee_ratio).toEqual(500);
  })

  test('bram create', async () => {
    const supply = 418945440768
    await contracts.ram.actions.create([BRAM_CONTRACT, `${supply} ${BRAM_SYMBOL}`]).send()
    expect(getTokenMaxSupply(BRAM_SYMBOL)).toEqual(supply);
  })

  test('deposit_eos: only transfer [eosio.token/EOS,ram.defi/BRAM]', async () => {
    let action = contracts.box.actions.transfer(["account1", BRAM_CONTRACT, "1.000000 BOX", ""]).send("account1@active");
    await expectToThrow(action, "eosio_assert: ram.defi::deposit: only transfer [eosio.token/EOS,ram.defi/BRAM] ");
  })

  test('deposit ram', async () => {
    const pay = 100;
    const bram = pay;
    const bram_after_fee = deposit_cost_with_fee(bram);

    const bram_before = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const rambank_ram_bytes_before = getRamBytes("rambank.defi");

    await contracts.eosio.actions.ramtransfer(["account1", "ram.defi", pay, ""]).send("account1@active");

    const bram_after = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const rambank_ram_bytes_after = getRamBytes("rambank.defi");

    expect(bram_after - bram_before).toEqual(bram_after_fee);
    expect(rambank_ram_bytes_after - rambank_ram_bytes_before).toEqual(pay);
  })

  test('withdraw ram', async () => {
    const pay = Asset.from("90 BRAM");
    const ram_bytes = pay.units.toNumber();
    const ram_bytes_after_fee = withdraw_cost_with_fee(ram_bytes);

    const bram_before = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const ramfees_bram_before = getTokenBalance("ramfees.defi", contracts.ram, BRAM_SYMBOL);
    const ram_bytes_before = getRamBytes("account1");
    const rambank_ram_bytes_before = getRamBytes("rambank.defi");

    await contracts.ram.actions.transfer(["account1", "ram.defi", pay.toString(), "ram"]).send("account1@active");

    const bram_after = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const ramfees_bram_after = getTokenBalance("ramfees.defi", contracts.ram, BRAM_SYMBOL);
    const ram_bytes_after = getRamBytes("account1");
    const rambank_ram_bytes_after = getRamBytes("rambank.defi");

    expect(bram_before - bram_after).toEqual(pay.units.toNumber());
    expect(ramfees_bram_after - ramfees_bram_before).toEqual(ram_bytes - ram_bytes_after_fee);
    expect(ram_bytes_after - ram_bytes_before).toEqual(ram_bytes_after_fee);
    expect(rambank_ram_bytes_before - rambank_ram_bytes_after).toEqual(ram_bytes_after_fee);
  })

  test('deposit eos', async () => {
    const pay = Asset.from("100.0000 EOS");
    let buyram_bytes = bytes_cost_with_fee(deposit_cost_with_fee(pay.units.toNumber()));

    const before = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    await contracts.eos.actions.transfer(["account1", "ram.defi", pay.toString(), ""]).send("account1@active");
    const after = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    expect(after - before).toEqual(buyram_bytes);
  })

  test('withdraw eos', async () => {
    const pay_bram = 831725;

    let sellram_eos = eos_receive_with_fee(pay_bram);
    let withdraw_eos = withdraw_cost_with_fee(sellram_eos);

    const eos_before = getTokenBalance("account1", contracts.eos, EOS_SYMBOL);
    const bram_before = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const ramfees_eos_before = getTokenBalance("ramfees.defi", contracts.eos, EOS_SYMBOL);
    const rambank_ram_bytes_before = getRamBytes("rambank.defi");

    await contracts.ram.actions.transfer(["account1", "ram.defi", `${pay_bram} ${BRAM_SYMBOL}`, ""]).send("account1@active");

    const eos_after = getTokenBalance("account1", contracts.eos, EOS_SYMBOL);
    const bram_after = getTokenBalance("account1", contracts.ram, BRAM_SYMBOL);
    const ramfees_eos_after = getTokenBalance("ramfees.defi", contracts.eos, EOS_SYMBOL);
    const rambank_ram_bytes_after = getRamBytes("rambank.defi");

    expect(rambank_ram_bytes_before - rambank_ram_bytes_after).toEqual(pay_bram);
    expect(bram_before - bram_after).toEqual(pay_bram);
    expect(eos_after - eos_before).toEqual(withdraw_eos);
    expect(ramfees_eos_after - ramfees_eos_before).toEqual(sellram_eos - withdraw_eos);
  })
});
