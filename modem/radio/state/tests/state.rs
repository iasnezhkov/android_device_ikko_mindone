/*
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 */

#![forbid(unsafe_code)]

use mindone_radio_state::model::*;
use mindone_radio_state::parse::{self, Context, Extensions, Family};
use mindone_radio_state::policy::*;

fn report(line: &str, family: Family) -> parse::RegistrationReport {
    parse::registration(line, family, Context::Unsolicited).unwrap()
}

fn registration() -> Registration {
    Registration::from_report(&report("+EREG: 0,\"FFFF\",\"FFFFFFF\",0,0,0,0,0", Family::Ereg)).unwrap().1
}

fn event(update: Update, observed_ms: u64) -> Event {
    Event { generation: 7, observed_ms, source: Source::Urc, update }
}

fn input(reporting: bool) -> Input { Input { filter: if reporting { KNOWN_BITS } else { 0 } } }

fn acknowledge(policy: &mut Policy, steps: Vec<Step>) {
    for step in steps { assert_eq!(policy.complete(step.id, true), Ok(None)); }
}

#[test]
fn ereg_cs_field_order_from_decompile() {
    let r = report("+EREG: 3,\"ABCD\",\"123456789ABC\",4096,1,7,8,15", Family::Ereg);
    assert_eq!((r.state, r.area, r.cell, r.rat), (3, Some(0xabcd), Some(0x123456789abc), Some(4096)));
    assert_eq!(r.extensions, Extensions::Cs { emergency_status: Some(1), unverified: vec![Some(7), Some(8)], reject_cause: Some(15) });
}

#[test]
fn egreg_ps_field_order_from_decompile() {
    let r = report("+EGREG: 3,00AF,123456789,8192,5000,7,15,1,2,3,4,5", Family::Egreg);
    assert_eq!(r.area, Some(0xaf));
    assert_eq!(r.cell, Some(0x123456789));
    assert_eq!(r.extensions, Extensions::Ps { bearer: Some(0x5000), capability: Some(7), reject_cause: Some(15), nr_and_status_unverified: vec![Some(1), Some(2), Some(3), Some(4), Some(5)] });
}

#[test]
fn solicited_and_unsolicited_require_explicit_context() {
    let query = parse::registration("+EGREG: 3,1,00AF,ABCDEF,4096,2000,0,0", Family::Egreg, Context::Solicited).unwrap();
    let urc = report("+EGREG: 1,00AF,ABCDEF,4096,2000,0,0", Family::Egreg);
    assert_eq!(query.mode, Some(3));
    assert_eq!(query.state, urc.state);
    assert_eq!(query.area, urc.area);
    assert!(parse::registration("+EGREG: 1,00AF,ABCDEF,4096,2000,0,0", Family::Egreg, Context::Solicited).is_err());
    assert!(parse::registration("+EREG: 3,1", Family::Ereg, Context::Unsolicited).is_err());
}

#[test]
fn solicited_ereg_layout_unverified_beyond_common_fields() {
    let r = parse::registration("+EREG: 3,0", Family::Ereg, Context::Solicited).unwrap();
    assert_eq!((r.mode, r.state, r.area), (Some(3), 0, None));
}

#[test]
fn plain_registration_layouts_are_explicitly_unverified() {
    for (family, prefix) in [(Family::CregUnverified, "CREG"), (Family::CgregUnverified, "CGREG"), (Family::CeregUnverified, "CEREG")] {
        let r = parse::registration(&format!("+{prefix}: 2,1,\"00AF\",\"ABCDE\",7"), family, Context::Solicited).unwrap();
        assert_eq!((r.state, r.area, r.cell, r.rat), (1, Some(175), Some(0xabcde), Some(7)));
        assert!(Registration::from_report(&r).is_none());
        let urc = report(&format!("+{prefix}: 1,00AF,ABCDE,7"), family);
        assert_eq!(urc.mode, None);
    }
}

#[test]
fn registration_empty_optional_fields_stay_unknown() {
    let r = report("+EREG: 0,\"\",\"\",-1", Family::Ereg);
    assert_eq!((r.area, r.cell, r.rat), (None, None, None));
    assert_eq!(report("+EREG: 0", Family::Ereg).state, 0);
}

#[test]
fn registration_rejects_malformed_hex_quotes_overflow_and_prefix() {
    for line in ["+EREG: 0,GG,1,0", "+EREG: 0,1,10000000000000000,0", "+EREG: 0,100000000,1,0", "+EREG: 0,\"12,1,0", "+EREG: x", "+EGREG: 0", "+EREG: 0,1,2,x"] {
        assert!(parse::registration(line, Family::Ereg, Context::Unsolicited).is_err(), "{line}");
    }
}

#[test]
fn ecsq_ten_fields_and_rat_discriminator() {
    let r = parse::ecsq("+ECSQ: 0,99,-400,0,0,-40,-400,4096,80,3").unwrap();
    assert_eq!(r.values[7], 4096);
    assert_eq!(r.nr_csi_unverified, None);
    let (rat, signal) = Signal::from_ecsq(&r);
    assert_eq!(rat, SignalRat::MtkUnverified(4096));
    assert_eq!(signal, Signal::Lte { rsrp: Some(100), rsrq: Some(10), rssnr: Some(200) });
}

#[test]
fn ecsq_nr_field_order_and_units_unverified() {
    let r = parse::ecsq("+ECSQ: 0,99,-400,0,0,-40,-400,32768,80,78,-44,-404,84").unwrap();
    assert_eq!(r.nr_csi_unverified, Some([-44, -404, 84]));
    assert!(matches!(Signal::from_ecsq(&r).1, Signal::QuarterUnitsUnverified { csi: Some([Some(-44), Some(-404), Some(84)]), .. }));
}

#[test]
fn ecsq_rejects_missing_extra_and_invalid_fields() {
    for line in ["+ECSQ: 1,2", "+ECSQ: 0,0,0,0,0,0,0,32768,0,0", "+ECSQ: 0,0,0,0,0,0,0,4096,0,0,1", "+ECSQ: 0,0,0,0,0,0,0,x,0,0"] {
        assert!(parse::ecsq(line).is_err());
    }
}

#[test]
fn ecsq_unknown_sentinels_and_extremes() {
    let r = parse::ecsq("+ECSQ: 0,99,32767,0,0,32767,32767,4096,32767,0").unwrap();
    assert_eq!(Signal::from_ecsq(&r).1, Signal::Lte { rsrp: None, rsrq: None, rssnr: None });
    let r = parse::ecsq("+ECSQ: 0,0,0,0,0,-2147483648,-2147483648,4096,-2147483648,0").unwrap();
    assert_eq!(Signal::from_ecsq(&r).1, Signal::Lte { rsrp: Some(140), rsrq: Some(20), rssnr: Some(-200) });
}

#[test]
fn eind_consumers_do_not_prove_radio_readiness() {
    for code in [1, 2, 32, 128] { assert_eq!(parse::eind(&format!("+EIND: {code}")).unwrap().code, code); }
    for line in ["+EIND:", "+EIND: -1", "+EIND: 1,2", "+EIND: ready"] { assert!(parse::eind(line).is_err()); }
}

#[test]
fn csq_known_unknown_and_invalid() {
    assert_eq!(parse::csq("+CSQ: 20,3").unwrap(), parse::Csq { rssi: Some(20), ber: Some(3) });
    assert_eq!(parse::csq("+CSQ: 99,99").unwrap(), parse::Csq { rssi: None, ber: None });
    for line in ["+CSQ: 32,0", "+CSQ: 0,8", "+CSQ: -1,0", "+CSQ: 1", "+CSQ: 1,2,3"] { assert!(parse::csq(line).is_err()); }
}

#[test]
fn unknown_until_first_verified_registration() {
    let mut model = Model::new(7);
    assert_eq!(model.cs.value, Knowledge::Unknown);
    assert_eq!(model.ps.value, Knowledge::Unknown);
    model.apply(event(Update::Radio(Radio::On), 1));
    assert_eq!(model.cs.value, Knowledge::Unknown);
    assert_eq!(model.apply(event(Update::Registration(Domain::Cs, registration()), 2)), Some(Change::Registration(Domain::Cs)));
    assert_eq!(model.ps.value, Knowledge::Unknown);
}

#[test]
fn c53_recurrence_pattern_synthetic_urc_updates_only_freshness() {
    let mut model = Model::new(7);
    let r = registration();
    assert!(model.apply(event(Update::Registration(Domain::Cs, r.clone()), 0)).is_some());
    let mut now = 0;
    for i in 0..100 {
        now += 30_000 + (i % 11) * 1000;
        assert_eq!(model.apply(event(Update::Registration(Domain::Cs, r.clone()), now)), None);
    }
    assert_eq!(model.cs.observed_ms, now);
}

#[test]
fn all_registration_semantic_fields_change_the_key() {
    let base = registration();
    for index in 0..8 {
        let mut model = Model::new(7);
        model.apply(event(Update::Registration(Domain::Ps, base.clone()), 0));
        let mut next = base.clone();
        match index {
            0 => next.state = 3, 1 => next.rat_mtk_unverified = Some(4096),
            2 => next.area = Some(12), 3 => next.cell = Some(34),
            4 => next.plmn = Some("44010".into()), 5 => next.reject_cause = Some(15),
            6 => next.emergency_status_unverified = Some(1), _ => next.rat_mtk_unverified = None,
        }
        assert_eq!(model.apply(event(Update::Registration(Domain::Ps, next), 1)), Some(Change::Registration(Domain::Ps)));
    }
}

#[test]
fn cs_ps_are_independent() {
    let mut model = Model::new(7);
    for domain in [Domain::Cs, Domain::Ps] {
        assert_eq!(model.apply(event(Update::Registration(domain, registration()), 1)), Some(Change::Registration(domain)));
    }
}

#[test]
fn stale_generation_and_timestamp_do_not_mutate() {
    let mut model = Model::new(7);
    model.apply(event(Update::Registration(Domain::Cs, registration()), 100));
    let mut next = registration();
    next.state = 3;
    let mut stale = event(Update::Registration(Domain::Cs, next.clone()), 200);
    stale.generation = 6;
    assert_eq!(model.apply(stale), None);
    assert_eq!(model.apply(event(Update::Registration(Domain::Cs, next), 99)), None);
    assert_eq!(model.cs.observed_ms, 100);
}

#[test]
fn radio_loss_and_off_are_distinct_and_deduplicated() {
    let mut model = Model::new(7);
    for (now, radio) in [Radio::Off, Radio::On, Radio::Unavailable].into_iter().enumerate() {
        assert_eq!(model.apply(event(Update::Radio(radio), now as u64)), Some(Change::Radio));
        assert_eq!(model.apply(event(Update::Radio(radio), now as u64)), None);
    }
}

#[test]
fn signal_per_rat_dedup_excludes_band_metadata() {
    let mut model = Model::new(7);
    let mut r = parse::ecsq("+ECSQ: 0,0,0,0,0,-40,-400,4096,80,3").unwrap();
    let (rat, signal) = Signal::from_ecsq(&r);
    assert_eq!(model.apply(event(Update::Signal(rat, signal), 1)), Some(Change::Signal(rat)));
    r.values[9] = 4;
    let (_, signal) = Signal::from_ecsq(&r);
    assert_eq!(model.apply(event(Update::Signal(rat, signal), 2)), None);
    let gsm = SignalRat::GsmCsq;
    assert_eq!(model.apply(event(Update::Signal(gsm, Signal::Gsm(parse::csq("+CSQ: 99,99").unwrap())), 3)), Some(Change::Signal(gsm)));
    assert_eq!(model.signals.len(), 2);
    assert_eq!(model.signals[&rat].observed_ms, 2);
}

#[test]
fn policy_reporting_transitions_and_idempotence() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(input(true)).unwrap();
    assert_eq!(steps.iter().map(|s| s.command.as_str()).collect::<Vec<_>>(), ["AT+ECSQ=1", "AT+EREG=3", "AT+EGREG=3", "AT+ESCREENSTATE=1"]);
    assert_eq!(steps.iter().map(|s| s.channel).collect::<Vec<_>>(), [1, 1, 1, 3]);
    assert!(policy.reconcile(input(true)).unwrap().is_empty());
    acknowledge(&mut policy, steps);
    assert!(policy.reconcile(input(true)).unwrap().is_empty());
    let steps = policy.reconcile(input(false)).unwrap();
    assert_eq!(steps.iter().map(|s| s.command.as_str()).collect::<Vec<_>>(), ["AT+ECSQ=0", "AT+EREG=9", "AT+EGREG=9", "AT+ESCREENSTATE=0"]);
    acknowledge(&mut policy, steps);
    assert!(policy.reconcile(input(false)).unwrap().is_empty());
    let steps = policy.reconcile(input(true)).unwrap();
    assert_eq!(steps.len(), 4);
}

#[test]
fn policy_filters_diff_independently() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(input(true)).unwrap();
    acknowledge(&mut policy, steps);
    let steps = policy.reconcile(Input { filter: FULL_NETWORK_STATE }).unwrap();
    assert_eq!(steps.len(), 1);
    assert_eq!(steps[0].command, "AT+ECSQ=0");
    acknowledge(&mut policy, steps);
    let steps = policy.reconcile(Input { filter: SIGNAL_STRENGTH }).unwrap();
    assert_eq!(steps.iter().map(|s| s.command.as_str()).collect::<Vec<_>>(), ["AT+ECSQ=1", "AT+EREG=9", "AT+EGREG=9"]);
}

#[test]
fn policy_mode_nine_error_fallback_is_second_step() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(input(false)).unwrap();
    policy.complete(steps[0].id, true).unwrap();
    for step in &steps[1..3] {
        let fallback = policy.complete(step.id, false).unwrap().unwrap();
        assert!(fallback.command.ends_with("=5"));
        assert_ne!(fallback.id, step.id);
        assert_eq!(policy.applied(step.control), None);
        assert_eq!(policy.complete(step.id, true), Err(PolicyError::UnexpectedCompletion));
        assert_eq!(policy.complete(fallback.id, true), Ok(None));
        assert_eq!(policy.applied(step.control), Some(false));
    }
    policy.complete(steps[3].id, true).unwrap();
    assert!(policy.reconcile(input(false)).unwrap().is_empty());
}

#[test]
fn policy_failures_retry_and_do_not_claim_applied() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(input(false)).unwrap();
    policy.complete(steps[0].id, false).unwrap();
    let fallback = policy.complete(steps[1].id, false).unwrap().unwrap();
    assert_eq!(policy.complete(fallback.id, false), Ok(None));
    acknowledge(&mut policy, steps[2..].to_vec());
    assert_eq!(policy.failed.len(), 2);
    assert_eq!(policy.applied(Control::Signal), None);
    assert_eq!(policy.applied(Control::Cs), None);
    let retry = policy.reconcile(input(false)).unwrap();
    assert_eq!(retry.len(), 2);
}

#[test]
fn policy_unknown_bits_and_unsupported_families_do_not_enable_commands() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(Input { filter: u32::MAX }).unwrap();
    acknowledge(&mut policy, steps);
    assert_eq!(policy.unknown_bits(), u32::MAX & !KNOWN_BITS);
    assert!(policy.registration_failure_enabled());
    assert!(policy.reconcile(Input { filter: SIGNAL_STRENGTH | FULL_NETWORK_STATE }).unwrap().is_empty());
    assert!(!policy.registration_failure_enabled());
}

#[test]
fn policy_reconnect_invalidates_pending_and_replays() {
    let mut policy = Policy::default();
    let old = policy.reconcile(input(true)).unwrap();
    policy.reconnect();
    let new = policy.reconcile(input(true)).unwrap();
    assert_eq!(new.len(), 4);
    assert_eq!(policy.complete(old[0].id, true), Err(PolicyError::UnexpectedCompletion));
    acknowledge(&mut policy, new);
}

#[test]
fn policy_desired_change_during_pending_is_reconciled_after_completion() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(input(true)).unwrap();
    assert!(policy.reconcile(input(false)).unwrap().is_empty());
    acknowledge(&mut policy, steps);
    assert_eq!(policy.reconcile(policy.desired.unwrap()).unwrap().len(), 4);
}

#[test]
fn verified_registration_identity_sentinels_stay_unknown() {
    let r = registration();
    assert_eq!((r.area, r.cell), (None, None));
    let r = Registration::from_report(&report("+EREG: 1,0000,0000,0", Family::Ereg)).unwrap().1;
    assert_eq!((r.area, r.cell), (Some(0), Some(0)));
}

#[test]
fn query_mode_and_source_are_excluded_from_registration_key() {
    let mut model = Model::new(7);
    let urc = report("+EREG: 0,FFFF,FFFFFFF,0,0,0,0,0", Family::Ereg);
    let query = parse::registration("+EREG: 3,0,FFFF,FFFFFFF,0,0,0,0,0", Family::Ereg, Context::Solicited).unwrap();
    let (domain, registration) = Registration::from_report(&urc).unwrap();
    assert!(model.apply(event(Update::Registration(domain, registration), 1)).is_some());
    let (domain, registration) = Registration::from_report(&query).unwrap();
    let mut query_event = event(Update::Registration(domain, registration), 2);
    query_event.source = Source::Query;
    assert_eq!(model.apply(query_event), None);
    assert_eq!(model.cs.observed_ms, 2);
    assert_eq!(model.cs.source, Source::Query);
}

#[test]
fn policy_follows_the_framework_filter_and_derives_the_screen_state_from_it() {
    let mut policy = Policy::default();
    let steps = policy.reconcile(Input { filter: REGISTRATION_FAILURE | BARRING_INFO }).unwrap();
    assert_eq!(steps.iter().map(|s| s.command.as_str()).collect::<Vec<_>>(), ["AT+ECSQ=0", "AT+EREG=9", "AT+EGREG=9", "AT+ESCREENSTATE=0"]);
    acknowledge(&mut policy, steps);
    let steps = policy.reconcile(Input { filter: FULL_NETWORK_STATE }).unwrap();
    assert_eq!(steps.iter().map(|s| s.command.as_str()).collect::<Vec<_>>(), ["AT+EREG=3", "AT+EGREG=3", "AT+ESCREENSTATE=1"]);
    acknowledge(&mut policy, steps);
    assert!(policy.reconcile(Input { filter: FULL_NETWORK_STATE }).unwrap().is_empty());
}

#[test]
fn sim_reports_preserve_unknown_and_unverified_metadata() {
    let mut model = Model::new(7);
    assert_eq!(model.sim.value.card, SimCard::Unknown);
    for line in ["+EUSIM: 1", "+ESIMS: 1,9", "+CPIN: CARD_REBOOT", "+CME ERROR: 14"] {
        assert_eq!(model.apply(event(Update::Sim(parse::sim(line).unwrap()), 1)), None);
        assert_eq!(model.sim.value.card, SimCard::Unknown);
    }
    assert_eq!(model.sim.value.eusim_unverified, [1]);
    assert_eq!(model.sim.value.esims_status_unverified, Some(9));
}

#[test]
fn sim_known_reports_deduplicate_and_refresh() {
    let mut model = Model::new(7);
    for (index, line) in ["+ESIMS: 0,0", "+CPIN: SIM PIN", "+CPIN: SIM PUK", "+CPIN: READY", "+CME ERROR: 13", "+CME ERROR: 10"].iter().enumerate() {
        let now = index as u64 + 1;
        let update = Update::Sim(parse::sim(line).unwrap());
        assert_eq!(model.apply(event(update.clone(), now)), Some(Change::Sim));
        assert_eq!(model.apply(event(update, now + 1)), None);
        assert_eq!(model.sim.observed_ms, now + 1);
    }
    assert_eq!(model.sim.value.card, SimCard::Absent);
    assert_eq!(model.sim.value.app, Knowledge::Unknown);
}

#[test]
fn sim_old_generation_and_old_timestamp_are_ignored() {
    let mut model = Model::new(7);
    model.apply(event(Update::Sim(parse::sim("+CPIN: READY").unwrap()), 10));
    let update = Update::Sim(parse::sim("+ESIMS: 0,0").unwrap());
    assert_eq!(model.apply(event(update.clone(), 9)), None);
    let mut stale = event(update, 20);
    stale.generation = 6;
    assert_eq!(model.apply(stale), None);
    assert_eq!(model.sim.value.card, SimCard::Present);
    assert_eq!(model.sim.observed_ms, 10);
}

#[test]
fn sim_enablement_and_vendor_lock_details_are_typed() {
    let mut model = Model::new(7);
    for (line, enabled) in [("+ESIMS: 0,28", false), ("+ESIMS: 1,29", true)] {
        assert_eq!(model.apply(event(Update::Sim(parse::sim(line).unwrap()), 1)), Some(Change::Sim));
        assert_eq!(model.sim.value.applications_enabled, Knowledge::Known(enabled));
        assert_eq!(model.sim.value.card, SimCard::Unknown);
    }
    assert!(matches!(parse::sim("+CPIN: PH-NET PIN"), Ok(parse::SimReport::Cpin(parse::SimApp::PersonalizationUnverified(_)))));
    assert!(matches!(parse::sim("+CPIN: EMPTY_EUICC"), Ok(parse::SimReport::Cpin(parse::SimApp::VendorUnverified(_)))));
}

#[test]
fn sim_malformed_reports_and_unrelated_errors_are_rejected() {
    for line in ["+ESIMS: 0", "+ESIMS: 2,0", "+ESIMS: x,0", "+EUSIM:", "+CPIN: unknown", "+CPIN: READY,0", "+CME ERROR: 54", "+CME ERROR: 10,0", "+CPIN: \"READY"] {
        assert!(parse::sim(line).is_err(), "{line}");
    }
}

#[test]
fn unverified_sim_reports_preserve_known_card_and_app_state() {
    let mut model = Model::new(7);
    model.apply(event(Update::Sim(parse::sim("+CPIN: READY").unwrap()), 1));
    for line in ["+CPIN: CARD_REBOOT", "+EUSIM: 0,1", "+ESIMS: 1,9"] {
        assert_eq!(model.apply(event(Update::Sim(parse::sim(line).unwrap()), 2)), None);
        assert_eq!(model.sim.value.card, SimCard::Present);
        assert_eq!(model.sim.value.app, Knowledge::Known(parse::SimApp::Ready));
        assert_eq!(model.sim.observed_ms, 2);
    }
}

#[test]
fn shared_observer_handles_registration_signal_and_sim_lines() {
    use mindone_radio_state::observe::line;
    for (text, domain) in [("+EREG: 1", Domain::Cs), ("+EGREG: 2", Domain::Ps)] {
        assert!(matches!(line(text), Some(Update::Registration(d, _)) if d == domain));
    }
    for text in ["+CPIN: READY", "+ESIMS: 0,0", "+EUSIM: 1", "+CME ERROR: 10", "+CME ERROR: 13", "+CME ERROR: 14", "+CME ERROR: 15"] {
        assert_eq!(line(text), Some(Update::Sim(parse::sim(text).unwrap())));
    }
    assert!(matches!(line("+ECSQ: 0,0,0,0,0,-40,-400,4096,40,0"), Some(Update::Signal(_, Signal::Lte { .. }))));
    for text in ["OK", "+EREG: bad", "+ECSQ: 0", "+CME ERROR: 42", "+CPIN: UNKNOWN"] { assert_eq!(line(text), None); }
}

#[test]
fn shared_ecsq_gsm_mapping_clamps_and_preserves_unknowns() {
    use mindone_radio_state::observe::line;
    for rat in [1, 2] {
        for (raw, rssi) in [(-400, Some(6)), (i32::MIN, Some(0)), (0, Some(31)), (32767, None), (i32::MAX, None)] {
            for (raw_ber, ber) in [(-1, None), (0, Some(0)), (7, Some(7)), (8, None), (99, None)] {
                let text = format!("+ECSQ: 0,{raw_ber},{raw},0,0,0,0,{rat},0,0");
                assert_eq!(line(&text), Some(Update::Signal(SignalRat::GsmCsq, Signal::Gsm(parse::Csq { rssi, ber }))));
                assert_eq!(Signal::from_ecsq(&parse::ecsq(&text).unwrap()), (SignalRat::GsmCsq, Signal::Gsm(parse::Csq { rssi, ber })));
            }
        }
    }
}
