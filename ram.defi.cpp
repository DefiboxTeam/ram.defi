#include <eosio.system/exchange_state.cpp>
#include <ram.defi.hpp>

using eosiosystem::rammarket;
using std::make_tuple;

[[eosio::action]]
void ram::create(const name& issuer, const asset& maximum_supply) {
    require_auth(get_self());

    check(issuer == get_self(), "issuer must be ram.defi");

    auto sym = maximum_supply.symbol;
    check(sym.is_valid(), "invalid symbol name");
    check(maximum_supply.is_valid(), "invalid supply");
    check(maximum_supply.amount > 0, "max-supply must be positive");

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing == statstable.end(), "token with symbol already exists");

    statstable.emplace(get_self(), [&](auto& s) {
        s.supply.symbol = maximum_supply.symbol;
        s.max_supply = maximum_supply;
        s.issuer = issuer;
    });
}

[[eosio::action]]
void ram::transfer(const name& from, const name& to, const asset& quantity, const string& memo) {
    check(from != to, "cannot transfer to self");
    require_auth(from);
    check(is_account(to), "to account does not exist");

    auto sym = quantity.symbol.code();
    stats statstable(get_self(), sym.raw());
    const auto& st = statstable.get(sym.raw());

    if (from != get_self()) {
        require_recipient(from);
    }
    if (to != get_self()) {
        require_recipient(to);
    }

    check(quantity.is_valid(), "invalid quantity");
    check(quantity.amount > 0, "must transfer positive quantity");
    check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
    check(memo.size() <= 256, "memo has more than 256 bytes");

    asset from_balance;
    asset to_balance = asset(0, quantity.symbol);
    if (to == get_self()) {
        if(memo == "ram"){
            do_withdraw_ram(from, quantity);
        }else {
            do_withdraw_eos(from, quantity);
        }
        // from balance

        accounts from_acnts(get_self(), from.value);
        auto from = from_acnts.require_find(quantity.symbol.code().raw(), "no balance object found");

        from_balance = from->balance;
    } else {
        auto payer = has_auth(to) ? to : from;

        from_balance = sub_balance(from, quantity);
        to_balance = add_balance(to, quantity, payer);
    }

    action(permission_level{_self, "active"_n}, _self, "transferlog"_n, make_tuple(from, to, quantity, from_balance, to_balance)).send();
}

[[eosio::action]]
void ram::updatestatus(bool disabled_deposit, bool disabled_withdraw) {
    require_auth(ADMIN_ACCOUNT);
    ram::config_table _config(get_self(), get_self().value);
    config_row config = _config.get_or_default();

    config.disabled_deposit = disabled_deposit;
    config.disabled_withdraw = disabled_withdraw;
    _config.set(config, get_self());
}

[[eosio::action]]
void ram::updateratio(uint16_t deposit_fee_ratio, uint16_t withdraw_fee_ratio) {
    require_auth(ADMIN_ACCOUNT);
    check(deposit_fee_ratio <= 5000, "ram.defi::updateratio: deposit_fee_ratio must be <= 5000");
    check(withdraw_fee_ratio <= 5000, "ram.defi::updateratio: withdraw_fee_ratio must be <= 5000");
    ram::config_table _config(get_self(), get_self().value);
    config_row config = _config.get_or_default();

    config.withdraw_fee_ratio = withdraw_fee_ratio;
    config.deposit_fee_ratio = deposit_fee_ratio;

    _config.set(config, get_self());
}

[[eosio::on_notify("*::ramtransfer")]]
void ram::on_ramtransfer(const name& from, const name& to, int64_t bytes, const std::string& memo ) {
    // ignore transfers
    if (to != get_self())
        return;

    // authenticate incoming `from` account
    require_auth(from);

    const name contract = get_first_receiver();
    check(contract == EOSIO_ACCOUNT, "ram.defi::depositram: only the eosio contract may send RAM bytes to this contract.");

    do_deposit_ram(from, bytes);
}

[[eosio::on_notify("*::transfer")]]
void ram::on_transfer(const name& from, const name& to, const asset& quantity, const string& memo) {
    // ignore transfers
    if (to != get_self())
        return;

    // authenticate incoming `from` account
    require_auth(from);

    const name contract = get_first_receiver();
    check(contract == EOS_CONTRACT && quantity.symbol == EOS, "ram.defi::deposit: only transfer [eosio.token/EOS,ram.defi/BRAM] ");

    do_deposit_eos(from, quantity);
}

void ram::do_deposit_ram(const name& owner, const int64_t bytes) {
    check( bytes > 0, "ram.defi::deposit: cannot reduce negative byte");

    ram::config_row _config = get_config();
    check(!_config.disabled_deposit, "ram.defi::deposit: deposit has been suspended");

    // transfer to rambank
    ram_transfer(get_self(), RAM_BANK_ACCOUNT, bytes, "deposit ram");

    asset output = {bytes, BRAM};

    issue(get_self(), output);

    auto deposit_fee = output * _config.deposit_fee_ratio / RATIO_PRECISION;
    auto buy_ram_bytes = output - deposit_fee;

    if (deposit_fee.amount > 0) {
        token_transfer(get_self(), RAM_FEES_ACCOUNT, extended_asset(deposit_fee, get_self()), "deposit fee");
    }

    token_transfer(get_self(), owner, extended_asset(buy_ram_bytes, get_self()), "deposit ram");

    // log
    ram::depositram_action depositram(get_self(), {get_self(), "active"_n});
    depositram.send(owner, bytes, deposit_fee, buy_ram_bytes);
}

void ram::do_deposit_eos(const name& owner, const asset& quantity) {

    ram::config_row _config = get_config();
    check(!_config.disabled_deposit, "ram.defi::deposit: deposit has been suspended");

    auto deposit_fee = quantity * _config.deposit_fee_ratio / RATIO_PRECISION;
    auto buy_ram_quantity = quantity - deposit_fee;

    if (deposit_fee.amount > 0) {
        token_transfer(get_self(), RAM_FEES_ACCOUNT, extended_asset(deposit_fee, EOS_CONTRACT), "deposit fee");
    }

    // buy ram
    auto output_amount = buyram(get_self(), RAM_BANK_ACCOUNT, buy_ram_quantity);
    auto output_bram = asset(output_amount.amount, BRAM);

    // issue
    issue(owner, output_bram);

    // log
    ram::depositlog_action depositlog(get_self(), {get_self(), "active"_n});
    depositlog.send(owner, quantity, deposit_fee, output_bram);
}

void ram::do_withdraw_ram(const name& owner, const asset& quantity) {
    ram::config_row _config = get_config();
    check(!_config.disabled_withdraw, "ram.defi::withdraw: withdraw has been suspended");

    auto withdraw_fee = quantity * _config.withdraw_fee_ratio / RATIO_PRECISION;
    auto to_account_bytes = quantity - withdraw_fee;

    // retire
    retire(owner, to_account_bytes);

    // transfer fee
    if(withdraw_fee.amount > 0){
        token_transfer(get_self(), RAM_FEES_ACCOUNT, extended_asset(withdraw_fee, get_self()), "withdraw fee");
    }
    // transfer ram
    ram_transfer(RAM_BANK_ACCOUNT, owner, to_account_bytes.amount, "withdraw ram");

    // log
    ram::withdrawram_action withdrawram(get_self(), {get_self(), "active"_n});
    withdrawram.send(owner, quantity, withdraw_fee, to_account_bytes.amount);
}

void ram::do_withdraw_eos(const name& owner, const asset& quantity) {
    ram::config_row _config = get_config();
    check(!_config.disabled_withdraw, "ram.defi::withdraw: withdraw has been suspended");

    // retire
    retire(owner, quantity);

    // sellram
    asset output_amount = sellram(RAM_BANK_ACCOUNT, quantity.amount);

    auto withdraw_fee = output_amount * _config.withdraw_fee_ratio / RATIO_PRECISION;
    auto to_account = output_amount - withdraw_fee;

    // collect fee
    if (withdraw_fee.amount > 0) {
        token_transfer(RAM_BANK_ACCOUNT, RAM_FEES_ACCOUNT, extended_asset(withdraw_fee, EOS_CONTRACT), "withdraw fee");
    }

    // transfer to owner
    token_transfer(RAM_BANK_ACCOUNT, owner, extended_asset(to_account, EOS_CONTRACT), "withdraw");

    // log
    ram::withdrawlog_action withdrawlog(get_self(), {get_self(), "active"_n});
    withdrawlog.send(owner, quantity, withdraw_fee, to_account);
}

asset ram::buyram(const name& payer, const name& receiver, const asset& quantity) {
    auto fee = quantity;
    fee.amount = (fee.amount + 199) / 200;  /// .5% fee (round up)
    // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
    // If quant.amount == 1, then fee.amount == 1,
    // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
    auto quantity_after_fee = quantity;
    quantity_after_fee.amount -= fee.amount;
    auto output_amount = convert(quantity_after_fee, RAM);

    action(permission_level{payer, "active"_n}, EOSIO_ACCOUNT, name("buyram"), make_tuple(payer, receiver, quantity)).send();

    return output_amount;
}

asset ram::sellram(const name& account, int64_t bytes) {
    auto output_amount = convert(asset(static_cast<uint64_t>(bytes), RAM), EOS);

    auto fee = (output_amount.amount + 199) / 200;  /// .5% fee (round up)
    output_amount.amount -= fee;

    action(permission_level{account, "active"_n}, EOSIO_ACCOUNT, name("sellram"), make_tuple(account, bytes)).send();
    return output_amount;
}

void ram::issue(const name& to, const asset& quantity) {
    auto sym = quantity.symbol;
    stats statstable(_self, sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing != statstable.end(), "token with symbol does not exist, create token before issue");
    const auto& st = *existing;

    check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

    statstable.modify(st, same_payer, [&](auto& s) { s.supply += quantity; });

    add_balance(st.issuer, quantity, st.issuer);
    if (to != st.issuer) {
        ram::transfer_action transfer_act(get_self(), {get_self(), "active"_n});
        transfer_act.send(st.issuer, to, quantity, "issue");
    }
}

void ram::retire(const name& owner, const asset& quantity) {
    auto sym = quantity.symbol;

    stats statstable(get_self(), sym.code().raw());
    auto existing = statstable.find(sym.code().raw());
    check(existing != statstable.end(), "token with symbol does not exist");
    const auto& st = *existing;

    statstable.modify(st, same_payer, [&](auto& s) { s.supply -= quantity; });

    sub_balance(owner, quantity);
}

asset ram::sub_balance(const name& owner, const asset& value) {
    accounts from_acnts(get_self(), owner.value);

    auto from = from_acnts.require_find(value.symbol.code().raw(), "no balance object found");

    check(from->balance.amount >= value.amount, "overdrawn balance");

    from_acnts.modify(from, owner, [&](auto& a) { a.balance -= value; });
    return from->balance;
}

asset ram::add_balance(const name& owner, const asset& value, const name& ram_payer) {
    accounts to_acnts(get_self(), owner.value);
    auto to = to_acnts.find(value.symbol.code().raw());
    if (to == to_acnts.end()) {
        to = to_acnts.emplace(ram_payer, [&](auto& a) { a.balance = value; });
    } else {
        to_acnts.modify(to, same_payer, [&](auto& a) { a.balance += value; });
    }
    return to->balance;
}

void ram::token_transfer(const name& from, const name& to, const extended_asset& value, const string& memo) {
    ram::transfer_action transfer(value.contract, {from, "active"_n});
    transfer.send(from, to, value.quantity, memo);
}

void ram::ram_transfer(const name& from, const name& to, const int64_t bytes, const string& memo) {
    action(permission_level{from, "active"_n}, EOSIO_ACCOUNT, "ramtransfer"_n, make_tuple(from, to, bytes, memo)).send();
}

asset ram::convert(const asset& from, const symbol& to) {
    rammarket _rammarket(EOSIO_ACCOUNT, EOSIO_ACCOUNT.value);
    auto itr = _rammarket.find(RAMCORE.raw());
    auto itr_tmp = *itr;
    return itr_tmp.convert(from, to);
}

ram::config_row ram::get_config() {
    ram::config_table _config(get_self(), get_self().value);
    return _config.get_or_default();
}
