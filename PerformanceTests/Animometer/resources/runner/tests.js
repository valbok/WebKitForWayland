var Headers = {
    testName: [{ title: Strings.text.testName }],
    score: [{ title: Strings.text.score, text: Strings.json.score }]
};

var Suite = function(name, tests) {
    this.name = name;
    this.tests = tests;
};

var Suites = [];

Suites.push(new Suite("Animometer",
    [
        {
            url: "master/canvas-stage.html?pathType=arcs",
            name: "Canvas arcs"
        },
        {
            url: "master/canvas-stage.html?pathType=linePath&lineJoin=round&lineCap=round",
            name: "Canvas line path, round join"
        },
        {
            url: "master/canvas-stage.html?pathType=line&lineCap=square",
            name: "Canvas line segments"
        },
    ]
));

function suiteFromName(name)
{
    return Suites.find(function(suite) { return suite.name == name; });
}

function testFromName(suite, name)
{
    return suite.tests.find(function(test) { return test.name == name; });
}
