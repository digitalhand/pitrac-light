# PiTrac Module Dependencies

**Analysis Date:** /home/jesher/Code/Github/digitalhand/pitrac-light

---

## Summary

- **Total Files Analyzed:** 206
- **Total Modules:** 12
- **Circular Dependencies:** 9

## Modules

| Module | Depends On | Dependents |
|--------|------------|------------|
| Camera | 0 | 0 |
| ImageAnalysis | 0 | 0 |
| core | 8 | 9 |
| encoder | 1 | 1 |
| image | 1 | 2 |
| output | 1 | 1 |
| post_processing_stages | 3 | 1 |
| preview | 1 | 1 |
| sim/common | 3 | 2 |
| sim/gspro | 3 | 2 |
| tests | 2 | 0 |
| utils | 1 | 5 |

## Detailed Module Dependencies

### Camera

**No dependencies**

**Not used by other modules**

---

### ImageAnalysis

**No dependencies**

**Not used by other modules**

---

### core

**Depends on:**
- `encoder`
- `image`
- `output`
- `post_processing_stages`
- `preview`
- `sim/common`
- `sim/gspro`
- `utils`

**Used by:**
- `encoder`
- `image`
- `output`
- `post_processing_stages`
- `preview`
- `sim/common`
- `sim/gspro`
- `tests`
- `utils`

---

### encoder

**Depends on:**
- `core`

**Used by:**
- `core`

---

### image

**Depends on:**
- `core`

**Used by:**
- `core`
- `post_processing_stages`

---

### output

**Depends on:**
- `core`

**Used by:**
- `core`

---

### post_processing_stages

**Depends on:**
- `core`
- `image`
- `utils`

**Used by:**
- `core`

---

### preview

**Depends on:**
- `core`

**Used by:**
- `core`

---

### sim/common

**Depends on:**
- `core`
- `sim/gspro`
- `utils`

**Used by:**
- `core`
- `sim/gspro`

---

### sim/gspro

**Depends on:**
- `core`
- `sim/common`
- `utils`

**Used by:**
- `core`
- `sim/common`

---

### tests

**Depends on:**
- `core`
- `utils`

**Not used by other modules**

---

### utils

**Depends on:**
- `core`

**Used by:**
- `core`
- `post_processing_stages`
- `sim/common`
- `sim/gspro`
- `tests`

---

## ⚠️ Circular Dependencies

Found **9** circular dependency chains:

1. core → sim/gspro → core
2. core → sim/gspro → utils → core
3. core → sim/gspro → sim/common → core
4. sim/gspro → sim/common → sim/gspro
5. core → preview → core
6. core → post_processing_stages → image → core
7. core → post_processing_stages → core
8. core → output → core
9. core → encoder → core

**Action Required:** Circular dependencies should be broken by:
- Introducing interfaces/abstractions
- Moving shared code to a common module
- Using dependency injection

## Recommendations

### Highly Coupled Modules

Modules with more than 5 dependencies:

- **core**: 8 dependencies

Consider refactoring to reduce coupling.

### Widely Used Modules

Modules used by more than 5 other modules:

- **core**: used by 9 modules

These are good candidates for:
- Comprehensive testing
- API stability
- Documentation

