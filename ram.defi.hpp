#pragma once
#include <eosio/asset.hpp>
#include <eosio/eosio.hpp>
#include <eosio/singleton.hpp>
#include <eosio/system.hpp>

using namespace eosio;
using std::string;

class [[eosio::contract("ram.defi")]] ram : public contract {
   public:
    using contract::contract;

    // CONTRACTS
    const name EOSIO_ACCOUNT = "eosio"_n;
    const name EOS_CONTRACT = "eosio.token"_n;
    const name ADMIN_ACCOUNT = "admin.defi"_n;
    const name RAM_FEES_ACCOUNT = "ramfees.defi"_n;
    const name RAM_BANK_ACCOUNT = "rambank.defi"_n;

    // BASE SYMBOLS
    const symbol EOS = symbol{"EOS", 4};
    const symbol BRAM = symbol{"BRAM", 0};
    const symbol RAMCORE = symbol{"RAMCORE", 4};
    const symbol RAM = symbol{"RAM", 0};

    const uint16_t RATIO_PRECISION = 10000;

    /**
   * Create action.
   *
   * @details Allows `issuer` account to create a token in supply of
   * `maximum_supply`.
   * @param issuer - the account that creates the token,
   * @param maximum_supply - the maximum supply set for the token created.
   *
   * @pre Token symbol has to be valid,
   * @pre Token symbol must not be already created,
   * @pre maximum_supply has to be smaller than the maximum supply allowed by
   * the system: 1^62 - 1.
   * @pre Maximum supply must be positive;
   *
   * If validation is successful a new entry in statstable for token symbol
   * scope gets created.
   */
    [[eosio::action]]
    void create(const name& issuer, const asset& maximum_supply);

    /**
   * Transfer action.
   *
   * @details Allows `from` account to transfer to `to` account the `quantity`
   * tokens. One account is debited and the other is credited with quantity
   * tokens.
   *
   * @param from - the account to transfer from,
   * @param to - the account to be transferred to,
   * @param quantity - the quantity of tokens to be transferred,
   * @param memo - the memo string to accompany the transaction.
   */
    [[eosio::action]]
    void transfer(const name& from, const name& to, const asset& quantity, const string& memo);

    /**
   * Updatestatus action.
   *
   * @details Modifying Global Status.
   * - **authority**: `admin.defi`
   *
   * @param disabled_deposit - deposit status
   * @param disabled_withdraw - withdraw status
   *
   */
    [[eosio::action]]
    void updatestatus(bool disabled_deposit, bool disabled_withdraw);

    /**
   * Updateratio action.
   * - **authority**: `admin.defi`
   *
   * @param deposit_fee_ratio - deposit deductible expense ratio
   * @param withdraw_fee_ratio - withdraw deductible expense ratio
   *
   */
    [[eosio::action]]
    void updateratio(uint16_t deposit_fee_ratio, uint16_t withdraw_fee_ratio);

    [[eosio::action]]
    void depositlog(const name& owner, const asset& quantity, const asset& fee, const asset& output_amount) {
        require_auth(get_self());
    }

    [[eosio::action]]
    void withdrawlog(const name& owner, const asset& quantity, const asset& fee, const asset& output_amount) {
        require_auth(get_self());
    }

    [[eosio::action]]
    void transferlog(const name& from, const name& to, const asset& quantity, const asset& from_balance, const asset& to_balance) {
        require_auth(get_self());
    }

    [[eosio::action]]
    void depositram(const name& owner, const int64_t bytes, const asset& fee, const asset& output_amount) {
        require_auth(get_self());
    }

    [[eosio::action]]
    void withdrawram(const name& owner, const asset& quantity, const asset& fee, const int64_t output_amount) {
        require_auth(get_self());
    }

    [[eosio::on_notify("*::transfer")]]
    void on_transfer(const name& from, const name& to, const asset& quantity, const string& memo);

    [[eosio::on_notify("*::ramtransfer")]]
    void on_ramtransfer(const name& from, const name& to, int64_t bytes, const std::string& memo);

    // action wrappers
    using transfer_action = eosio::action_wrapper<"transfer"_n, &ram::transfer>;
    using depositlog_action = eosio::action_wrapper<"depositlog"_n, &ram::depositlog>;
    using withdrawlog_action = eosio::action_wrapper<"withdrawlog"_n, &ram::withdrawlog>;
    using depositram_action = eosio::action_wrapper<"depositram"_n, &ram::depositram>;
    using withdrawram_action = eosio::action_wrapper<"withdrawram"_n, &ram::withdrawram>;

   private:
    struct [[eosio::table]] s_account {
        asset balance;
        uint64_t primary_key() const { return balance.symbol.code().raw(); }
    };

    struct [[eosio::table]] s_stat {
        asset supply;
        asset max_supply;
        name issuer;

        uint64_t primary_key() const { return supply.symbol.code().raw(); }
    };

    struct [[eosio::table("config")]] config_row {
        bool disabled_deposit;
        bool disabled_withdraw;
        uint16_t deposit_fee_ratio = 50;
        uint16_t withdraw_fee_ratio = 50;
    };

    typedef eosio::multi_index<"accounts"_n, s_account> accounts;
    typedef eosio::multi_index<"stat"_n, s_stat> stats;
    typedef eosio::singleton<"config"_n, config_row> config_table;

    asset sub_balance(const name& owner, const asset& value);
    asset add_balance(const name& owner, const asset& value, const name& ram_payer);

    void issue(const name& to, const asset& quantity);
    void retire(const name& owner, const asset& quantity);

    config_row get_config();
    asset convert(const asset& from, const symbol& to);
    void do_deposit_eos(const name& owner, const asset& quantity);
    void do_deposit_ram(const name& owner, const int64_t bytes);
    void do_withdraw_eos(const name& owner, const asset& quantity);
    void do_withdraw_ram(const name& owner, const asset& quantity);
    asset buyram(const name& payer, const name& receiver, const asset& quantity);
    asset sellram(const name& account, int64_t bytes);

    void token_transfer(const name& from, const name& to, const extended_asset& value, const string& memo);
    void ram_transfer(const name& from, const name& to, const int64_t bytes, const string& memo);
};
