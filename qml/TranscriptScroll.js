.pragma library

function capture(view, followThreshold) {
    var minimumY = view.originY;
    var maximumY = Math.max(minimumY, view.contentHeight - view.height + minimumY);
    var distanceFromBottom = Math.max(0, maximumY - view.contentY);
    return {
        follow: view.count === 0 || distanceFromBottom <= followThreshold,
        contentY: view.contentY
    };
}

function restore(view, state) {
    view.forceLayout();
    if (state.follow) {
        view.positionViewAtEnd();
        // Variable-height delegates refine the estimated extent when instantiated.
        view.forceLayout();
        view.positionViewAtEnd();
        return;
    }

    var minimumY = view.originY;
    var maximumY = Math.max(minimumY, view.contentHeight - view.height + minimumY);
    view.contentY = Math.max(minimumY, Math.min(state.contentY, maximumY));
}
