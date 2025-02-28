(function() {

function TemplateCanvasObject(stage)
{
    // For the canvas stage, most likely you will need to create your
    // animated object since it's only draw time thing.

    // Fill in your object data.
}

TemplateCanvasObject.prototype = {
    _draw: function()
    {
        // Draw your object.
    },

    animate: function(timeDelta)
    {
        // Redraw the animated object. The last time this animated
        // item was drawn before 'timeDelta'.

        // Move your object.

        // Redraw your object.
        this._draw();
    }
};

TemplateCanvasStage = Utilities.createSubclass(Stage,
    function()
    {
        Stage.call(this);
    }, {

    initialize: function(benchmark)
    {
        Stage.prototype.initialize.call(this, benchmark);
        this.context = this.element.getContext("2d");

        // Define a collection for your objects.
    },

    tune: function(count)
    {
        // If count is -ve, -count elements need to be removed form the
        // stage. If count is +ve, +count elements need to be added to
        // the stage.

        // Change objects in the stage.

        // Return the number of all the elements in the stage.
        // This number is recorded in the sampled data.

        // Return the count of the objects in the stage.
        return 0;
    },

    animate: function(timeDelta)
    {
        // Animate the elements such that all of them are redrawn. Most
        // likely you will need to call TemplateCanvasObject.animate()
        // for all your animated objects here.

        // Most likely you will need to clear the canvas with every redraw.
        this.context.clearRect(0, 0, this.size.x, this.size.y);

        // Loop through all your objects and ask them to animate.
    }
});

TemplateCanvasBenchmark = Utilities.createSubclass(Benchmark,
    function(options)
    {
        Benchmark.call(this, new TemplateCanvasStage(), options);
    }
);

window.benchmarkClass = TemplateCanvasBenchmark;

})();