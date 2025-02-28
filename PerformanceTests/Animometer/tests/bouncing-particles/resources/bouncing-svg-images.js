(function() {

BouncingSvgImage = Utilities.createSubclass(BouncingSvgParticle,
    function(stage)
    {
        BouncingSvgParticle.call(this, stage, "image");

        var attrs = { x: 0, y: 0, width: this.size.x, height: this.size.y };
        var xlinkAttrs = { href: stage.imageSrc };
        this.element = DocumentExtension.createSvgElement("image", attrs, xlinkAttrs, stage.element);
        this._move();
    }
);

BouncingSvgImagesStage = Utilities.createSubclass(BouncingSvgParticlesStage,
    function()
    {
        BouncingSvgParticlesStage.call(this);
    }, {

    initialize: function(benchmark)
    {
        BouncingSvgParticlesStage.prototype.initialize.call(this, benchmark);
        this.imageSrc = benchmark.options["imageSrc"] || "resources/yin-yang.svg";
    },

    createParticle: function()
    {
        return new BouncingSvgImage(this);
    }
});

BouncingSvgImagesBenchmark = Utilities.createSubclass(Benchmark,
    function(options)
    {
        Benchmark.call(this, new BouncingSvgImagesStage(), options);
    }
);

window.benchmarkClass = BouncingSvgImagesBenchmark;

})();

