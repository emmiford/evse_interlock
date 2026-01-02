# Handoff Note

- In scope (completed): V1 data pipeline for Sidewalk telemetry (IoT Core  DynamoDB with TTL  S3 archival), IoT rule `sidewalk/#`, archive/query/aggregate Lambdas, E2E test publish+query, `device_config` seeded for the kept device.
- Out of scope (deferred to separate project): Device provisioning + flashing workflow (CSV batch provisioning, certificate creation, SMSN derivation, multi-device setup).
- Status: V1 backend is operational; additional devices can be added later without changes to the pipeline.
- Next steps (separate project): Define provisioning flow, device identity inputs, and automation scripts.
