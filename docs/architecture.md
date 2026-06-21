# Class diagrams

The object model in UML, grouped by subsystem. Diagrams are
[Mermaid](https://mermaid.js.org/) `classDiagram` blocks — they render directly on
GitHub and in most Markdown viewers.

Every domain type derives from `Object` and reads itself from the YAML book through
`Object::Configure(ObjectReader&)`; the `ObjectManager` builds the graph on demand
(see [Configuration & build](#configuration--build)).

`<|--` is inheritance (derived `<|--` base reads "derived is-a base"), `*--` is
composition/ownership, `-->` is a reference (non-owning pointer), and `..>` a
build/use dependency.

## Underlyings & assets

A single tradable name (`Equity` / `Forex`) **is** a single-asset underlying:
`Single` derives from `Underlying` directly (there is no `Mono` adapter). The
multi-asset shapes are `Composite` (quanto), `Basket` and `Rainbow`.

```mermaid
classDiagram
    class Object {
        <<abstract>>
        +Configure(ObjectReader&)
        +GetName() string
        +GetKind() string
    }
    class Asset {
        <<abstract>>
        -Currency _currency
        +GetForward(date) double
    }
    class Underlying {
        <<abstract>>
        -Correlation _correlation
        +GetForward(date, Currency) double
        +GetSingleSet() SingleSet
        +IsGriddable() bool
        +IsMono() bool
    }
    class Single {
        <<abstract>>
        -Volatility _volatility
        -double _spot
        +GetImplicitVol(strike, date) double
    }
    class Equity {
        -RepoCurve _repo
        -ContinuousDividendsCurve _continuousDividends
        -DiscreteDividends _discreteDividends
    }
    class Forex {
        -Currency _underlyingCurrency
        +IsForex() bool
    }
    class Composite {
        -Underlying _underlying
    }
    class Basket {
        <<abstract>>
        -Underlying[] _underlyingList
    }

    Object <|-- Asset
    Asset <|-- Underlying
    Underlying <|-- Single
    Single <|-- Equity
    Single <|-- Forex
    Underlying <|-- Composite
    Underlying <|-- Basket
    Basket <|-- AbsoluteBasket
    Basket <|-- Rainbow

    Asset --> Currency : priced in
    Single --> Volatility : surface
    Underlying --> Correlation : quanto
    Composite --> Underlying : wraps
    Basket --> "*" Underlying : components
```

## Market data

```mermaid
classDiagram
    class MarketData {
        <<abstract>>
        +ApplyShift(factor, shift)
        +HasFactor(factor) bool
    }
    class Curve {
        <<abstract>>
        -date[] _dateList
        -LaVector _valueList
        +GetCurveValue(date) double
    }
    class Volatility {
        <<abstract>>
        +GetImplicitVol(strike, fwd, date) double
        +GetLocalVolatility(...) double
        +IsStochastic() bool
    }

    Object <|-- MarketData
    MarketData <|-- Curve
    MarketData <|-- Volatility
    MarketData <|-- DiscreteDividends
    MarketData <|-- Currency
    MarketData <|-- Correlation
    Curve <|-- YieldCurve
    Curve <|-- RepoCurve
    Curve <|-- ContinuousDividendsCurve
    Volatility <|-- BsVolatility
    Volatility <|-- SabrVolatility
    Volatility <|-- HestonVolatility

    Currency --> YieldCurve : discount rate
    Correlation --> "*" Forex : FX legs
```

## Contracts & book

`Contract` mixes in the per-engine pricing facets (`PdePriceable` / `AnaPriceable` /
`GpuPriceable`); a `Book` is the list of contracts priced together.

```mermaid
classDiagram
    class Contract {
        <<abstract>>
        -Underlying _underlying
        -Currency _premiumCurrency
        +Intrinsic(spot) double
        +GetFlowNode(...) MonteCarloNode
    }
    class PdePriceable {
        <<interface>>
    }
    class AnaPriceable {
        <<interface>>
    }
    class GpuPriceable {
        <<interface>>
    }

    Object <|-- Contract
    PdePriceable <|.. Contract
    AnaPriceable <|.. Contract
    GpuPriceable <|.. Contract
    Contract <|-- Vanilla
    Contract <|-- Barrier
    Contract <|-- VarianceSwap

    Object <|-- Book
    Book --> "*" Contract : options
    Contract --> Underlying
    Contract --> Currency : premium
```

## Tasks & pricers

The registry picks the concrete `Pricer` from the configuration's `method`
(`mcl` / `pde` / `ana`; GPU is the `mcl` engine with `allow_gpu`). A `Sequence`
runs a list of sub-tasks.

```mermaid
classDiagram
    class Task {
        <<abstract>>
        -YamlConfig _cfg
        +Execute()
        +WriteResults()
    }
    class Pricer {
        <<abstract>>
        -Book _book
        -PricerConfiguration _configuration
        -Correlation _correlation
        +PriceBook()*
    }

    Object <|-- Task
    Task <|-- Pricer
    Task <|-- Sequence
    Pricer <|-- PricerMCL
    Pricer <|-- PricerPDE
    Pricer <|-- PricerANA

    Pricer --> Book
    Pricer --> Currency
    Pricer --> PricerConfiguration
    Pricer --> Correlation
    Pricer --> DebugConfiguration
    Sequence --> "*" Task : sub-tasks

    Object <|-- PricerConfiguration
    Object <|-- MclConfiguration
    Object <|-- PdeConfiguration
    Object <|-- DebugConfiguration
    PricerConfiguration --> MclConfiguration
    PricerConfiguration --> PdeConfiguration
```

## Configuration & build

`ObjectManager` owns the parsed `YamlConfig` and the `ObjectCollector` (the
name-keyed object store). Each kind tag maps to a factory in the registry
(`object_registry.cpp`): it creates the bare object, registers it, then calls
`Configure`, which reads the object's own fields and references through an
`ObjectReader` bound to the manager.

```mermaid
classDiagram
    class ObjectManager {
        -YamlConfig _yml
        -ObjectCollector _collector
        +Get~T~(name) T
        +Build(name) Object
    }
    class ObjectCollector {
        +Add~T~(obj) T
        +Get~T~(name) T
    }
    class YamlConfig {
        +Get~T~(path) T
        +Set...(path, value)
    }
    class ObjectReader {
        -ObjectManager _m
        +Get~T~(field) T
        +Has~T~(field) bool
        +Ref~T~(field) T
    }

    ObjectManager *-- YamlConfig : owns
    ObjectManager *-- ObjectCollector : owns
    ObjectReader --> ObjectManager : binds
    ObjectManager ..> Object : builds (registry)
    Object ..> ObjectReader : Configure reads via
```

## Monte-Carlo node graph

The MCL/AMC engine builds a DAG of `MonteCarloNode`s (one flat hierarchy). Contracts
and market-data objects emit their nodes (`GetFlowNode` / `GetNode`); see
[monte_carlo.md](monte_carlo.md).

```mermaid
classDiagram
    class MonteCarloNode {
        <<abstract>>
        +Evaluate(...)
    }
    MonteCarloNode <|-- BrownianNode
    MonteCarloNode <|-- NoiseNode
    MonteCarloNode <|-- CorrelatedNoiseNode
    MonteCarloNode <|-- SpotDiffusionNode
    MonteCarloNode <|-- DriftNode
    MonteCarloNode <|-- DividendNode
    MonteCarloNode <|-- LocalVolatilityNode
    MonteCarloNode <|-- ConstantNode
    MonteCarloNode <|-- YieldCurveNode
    MonteCarloNode <|-- JumpNode
    MonteCarloNode <|-- HestonVarianceNode
    MonteCarloNode <|-- HestonSpotNode
    MonteCarloNode <|-- QuantoAdjustmentNode
    MonteCarloNode <|-- CompositeVolNode
    MonteCarloNode <|-- CompositeCorrelNode
    MonteCarloNode <|-- AbsoluteBasketNode
    MonteCarloNode <|-- RainbowNode
    MonteCarloNode <|-- ProductNode
    MonteCarloNode <|-- ContractNode
    MonteCarloNode <|-- BookNode
    MonteCarloNode <|-- VanillaFlowNode
    MonteCarloNode <|-- BarrierFlowNode
    MonteCarloNode <|-- VarianceSwapFlowNode
```
